#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <time.h>
#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "auth_manager.h"
#include "board_pins.h"
#include "config_store.h"
#include "io_manager.h"
#include "mqtt_manager.h"
#include "net_manager.h"
#include "sensor_manager.h"
#include "snmp_agent.h"
#include "time_manager.h"
#include "rest_api.h"

static const char *TAG = "rest_api";
static const char *WWW_BASE_PATH = "/www";

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE 4096
#define MAX_ROUTES 32

static char s_scratch[SCRATCH_BUFSIZE];

typedef struct {
    esp_err_t (*handler)(httpd_req_t *req);
} route_ctx_t;

static route_ctx_t s_route_ctx[MAX_ROUTES];
static int s_route_ctx_count;

static esp_err_t mount_www(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = WWW_BASE_PATH,
        .partition_label = "www",
        .max_files = 8,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS-Mount (\"www\") fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("www", &total, &used);
    ESP_LOGI(TAG, "Web-UI-Dateisystem gemountet: %u/%u Bytes belegt", (unsigned)used, (unsigned)total);
    return ESP_OK;
}

static void set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    size_t len = strlen(filepath);
    if (len > 5 && strcmp(filepath + len - 5, ".html") == 0) {
        type = "text/html";
    } else if (len > 3 && strcmp(filepath + len - 3, ".js") == 0) {
        type = "application/javascript";
    } else if (len > 4 && strcmp(filepath + len - 4, ".css") == 0) {
        type = "text/css";
    } else if (len > 5 && strcmp(filepath + len - 5, ".json") == 0) {
        type = "application/json";
    } else if (len > 4 && strcmp(filepath + len - 4, ".svg") == 0) {
        type = "image/svg+xml";
    } else if (len > 4 && strcmp(filepath + len - 4, ".ico") == 0) {
        type = "image/x-icon";
    }
    httpd_resp_set_type(req, type);
}

static esp_err_t static_file_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    strlcpy(filepath, WWW_BASE_PATH, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }

    int fd = open(filepath, O_RDONLY, 0);
    if (fd < 0) {
        ESP_LOGW(TAG, "Datei nicht gefunden: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Datei nicht gefunden");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    ssize_t read_bytes;
    do {
        read_bytes = read(fd, s_scratch, SCRATCH_BUFSIZE);
        if (read_bytes > 0 && httpd_resp_send_chunk(req, s_scratch, read_bytes) != ESP_OK) {
            close(fd);
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Senden fehlgeschlagen");
            return ESP_FAIL;
        }
    } while (read_bytes > 0);

    close(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Sendet `json` als "application/json"-Antwort und gibt es anschliessend frei.
static esp_err_t send_json(httpd_req_t *req, cJSON *json)
{
    const char *body = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body ? body : "null");
    free((void *)body);
    cJSON_Delete(json);
    return err;
}

// Liest den kompletten Request-Body (max. SCRATCH_BUFSIZE-1 Bytes) in
// s_scratch und haengt ein Null-Terminator an. Bei Ueberschreiten des
// Puffers oder Lesefehlern wird eine Fehlerantwort gesendet und ESP_FAIL
// geliefert.
static esp_err_t read_body_to_scratch(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body fehlt oder ist zu gross");
        return ESP_FAIL;
    }
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, s_scratch + received, total - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Lesen des Bodys fehlgeschlagen");
            return ESP_FAIL;
        }
        received += r;
    }
    s_scratch[received] = '\0';
    return ESP_OK;
}

static esp_err_t api_sensors_get_handler(httpd_req_t *req)
{
    cJSON *readings = NULL;
    sensor_manager_get_readings_json(&readings);
    return send_json(req, readings);
}

static esp_err_t api_sensors_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    sensor_manager_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_sensors_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = sensor_manager_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                              "Ungueltige Sensor-Konfiguration (GPIO-Konflikt oder nicht auf der Stiftleiste)");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_sensors_dallas_scan_get_handler(httpd_req_t *req)
{
    char query[64];
    char gpio_str[8];
    int gpio = -1;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "gpio", gpio_str, sizeof(gpio_str)) == ESP_OK) {
        char *endptr = NULL;
        long parsed = strtol(gpio_str, &endptr, 10);
        if (endptr != gpio_str && *endptr == '\0' && parsed >= 0 && parsed <= 54) {
            gpio = (int)parsed;
        }
    }
    if (gpio < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query-Parameter \"gpio\" fehlt oder ungueltig");
        return ESP_FAIL;
    }

    cJSON *rom_ids = NULL;
    esp_err_t err = sensor_manager_scan_dallas_bus_json(gpio, &rom_ids);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "GPIO liegt nicht auf der Stiftleiste");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }
    return send_json(req, rom_ids);
}

static esp_err_t api_io_get_handler(httpd_req_t *req)
{
    cJSON *states = NULL;
    io_manager_get_states_json(&states);
    return send_json(req, states);
}

static esp_err_t api_io_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    io_manager_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_io_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = io_manager_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                              "Ungueltige IO-Konfiguration (GPIO-Konflikt oder nicht auf der Stiftleiste)");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_io_set_post_handler(httpd_req_t *req)
{
    char query[64];
    char id[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Query-Parameter \"id\" fehlt");
        return ESP_FAIL;
    }
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    const cJSON *jstate = body ? cJSON_GetObjectItem(body, "state") : NULL;
    if (!cJSON_IsBool(jstate)) {
        cJSON_Delete(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Feld \"state\" (bool) fehlt");
        return ESP_FAIL;
    }
    bool state = cJSON_IsTrue(jstate);
    cJSON_Delete(body);

    esp_err_t err = io_manager_set_output(id, state);
    if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unbekannte IO-ID");
        return ESP_FAIL;
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "Ausgang hat eine aktive Schwellwert-Regel");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_network_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    net_manager_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_network_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = net_manager_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltige Netzwerk-Konfiguration");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_mqtt_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    mqtt_manager_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_mqtt_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = mqtt_manager_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                              "Ungueltige MQTT-Konfiguration (broker_uri muss mit mqtt:// oder mqtts:// beginnen)");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_snmp_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    snmp_agent_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_snmp_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = snmp_agent_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltige SNMP-Konfiguration");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_time_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    time_manager_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_time_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = time_manager_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltige Zeit-Konfiguration");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_auth_config_get_handler(httpd_req_t *req)
{
    cJSON *config = NULL;
    auth_manager_get_config_json(&config);
    return send_json(req, config);
}

static esp_err_t api_auth_config_put_handler(httpd_req_t *req)
{
    if (read_body_to_scratch(req) != ESP_OK) {
        return ESP_FAIL;
    }
    cJSON *body = cJSON_Parse(s_scratch);
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungueltiges JSON");
        return ESP_FAIL;
    }
    esp_err_t err = auth_manager_set_config_json(body);
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                              "Login kann nicht ohne Passwort aktiviert werden");
        return ESP_FAIL;
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speichern fehlgeschlagen");
        return ESP_FAIL;
    }
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_system_info_get_handler(httpd_req_t *req)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "idf_version", IDF_VER);
    cJSON_AddStringToObject(root, "chip", CONFIG_IDF_TARGET);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap", esp_get_minimum_free_heap_size());
    cJSON_AddBoolToObject(root, "has_ip", net_manager_has_ip());
    cJSON_AddBoolToObject(root, "wifi_connected", net_manager_wifi_connected());
    cJSON_AddBoolToObject(root, "eth_connected", net_manager_eth_connected());
    cJSON_AddBoolToObject(root, "mqtt_enabled", mqtt_manager_is_enabled());
    cJSON_AddBoolToObject(root, "mqtt_connected", mqtt_manager_is_connected());
    cJSON_AddBoolToObject(root, "snmp_enabled", snmp_agent_is_enabled());
    cJSON_AddBoolToObject(root, "snmp_listening", snmp_agent_is_listening());
    cJSON_AddBoolToObject(root, "auth_enabled", auth_manager_is_enabled());
    cJSON_AddNumberToObject(root, "header_gpio_count", (double)board_header_gpio_count);

    cJSON_AddBoolToObject(root, "time_synced", time_manager_is_synced());
    cJSON_AddNumberToObject(root, "unix_time_s", (double)time(NULL));
    cJSON_AddNumberToObject(root, "utc_offset_s", (double)time_manager_get_utc_offset_s());

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "{}");
    free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_system_reboot_post_handler(httpd_req_t *req)
{
    send_json(req, cJSON_CreateBool(true));
    vTaskDelay(pdMS_TO_TICKS(300)); // Zeit, damit die Antwort noch rausgeht
    esp_restart();
    return ESP_OK; // unreachable
}

static esp_err_t api_system_factory_reset_post_handler(httpd_req_t *req)
{
    config_store_erase(CONFIG_STORE_KEY_NETWORK);
    config_store_erase(CONFIG_STORE_KEY_MQTT);
    config_store_erase(CONFIG_STORE_KEY_SNMP);
    config_store_erase(CONFIG_STORE_KEY_SENSORS);
    config_store_erase(CONFIG_STORE_KEY_IO);
    config_store_erase(CONFIG_STORE_KEY_AUTH);
    send_json(req, cJSON_CreateBool(true));
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK; // unreachable
}

// Nimmt ein Firmware-Image (.bin) als rohen Request-Body entgegen und
// schreibt es direkt in den jeweils inaktiven OTA-Slot. Nach Erfolg wird
// dieser Slot als neuer Boot-Partition markiert und das Geraet neu
// gestartet. Dank CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE faellt der
// Bootloader automatisch auf die vorherige Firmware zurueck, falls die neue
// nicht startet (siehe esp_ota_mark_app_valid_cancel_rollback() in
// app_main.c).
static esp_err_t api_system_ota_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Kein Firmware-Image im Request-Body");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Keine OTA-Partition verfuegbar");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin fehlgeschlagen: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin fehlgeschlagen");
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int chunk = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, buf, chunk);
        if (received <= 0) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Uebertragung abgebrochen");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write fehlgeschlagen: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write fehlgeschlagen");
            return ESP_FAIL;
        }
        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end fehlgeschlagen: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                              err == ESP_ERR_OTA_VALIDATE_FAILED
                                  ? "Firmware-Image ungueltig (Signatur/Checksumme)"
                                  : "esp_ota_end fehlgeschlagen");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition fehlgeschlagen: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_set_boot_partition fehlgeschlagen");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA-Update erfolgreich, starte neu auf Partition \"%s\"", update_partition->label);
    send_json(req, cJSON_CreateBool(true));
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; // unreachable
}

// Nimmt ein neues SPIFFS-Image fuer das Web-UI (aus `idf.py build`:
// build/www.bin) als rohen Request-Body entgegen und schreibt es direkt in
// die "www"-Partition (kein esp_ota_*, das ist nur fuer App-Partitionen -
// hier wird die Partition manuell ausgehaengt, komplett geloescht und neu
// beschrieben). Anders als /api/system/ota ist danach KEIN Neustart noetig;
// die Partition wird sofort wieder eingehaengt. Waehrend des Uploads ist das
// Web-UI kurzzeitig nicht erreichbar (die REST-API selbst bleibt es, da sie
// Teil der Firmware und nicht von SPIFFS abhaengig ist).
static esp_err_t api_system_ota_www_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Kein Dateisystem-Image im Request-Body");
        return ESP_FAIL;
    }

    const esp_partition_t *www_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "www-Partition nicht gefunden");
        return ESP_FAIL;
    }
    if ((size_t)req->content_len > www_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image ist groesser als die www-Partition");
        return ESP_FAIL;
    }

    // Aushaengen, bevor die zugrundeliegende Flash-Partition geloescht wird -
    // sonst koennte der SPIFFS-Treiber waehrenddessen auf inkonsistente
    // Daten zugreifen.
    esp_vfs_spiffs_unregister("www");

    esp_err_t err = esp_partition_erase_range(www_partition, 0, www_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_erase_range (www) fehlgeschlagen: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Loeschen der www-Partition fehlgeschlagen");
        mount_www();
        return ESP_FAIL;
    }

    char buf[1024];
    size_t remaining = (size_t)req->content_len;
    size_t offset = 0;
    while (remaining > 0) {
        int chunk = remaining < sizeof(buf) ? (int)remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, buf, chunk);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Uebertragung abgebrochen");
            mount_www();
            return ESP_FAIL;
        }
        err = esp_partition_write(www_partition, offset, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_partition_write (www) fehlgeschlagen: %s", esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Schreiben der www-Partition fehlgeschlagen");
            mount_www();
            return ESP_FAIL;
        }
        offset += (size_t)received;
        remaining -= (size_t)received;
    }

    err = mount_www();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web-UI-Dateisystem nach Update ungueltig: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                              "Web-UI-Dateisystem ungueltig - bitte per USB neu flashen");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Web-UI-Update erfolgreich (%u Bytes)", (unsigned)offset);
    return send_json(req, cJSON_CreateBool(true));
}

static esp_err_t api_board_pins_get_handler(httpd_req_t *req)
{
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < board_header_gpio_count; i++) {
        const board_header_gpio_t *p = &board_header_gpios[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "gpio", p->gpio);
        cJSON_AddNumberToObject(item, "header_pin", p->header_pin);
        if (p->note != NULL) {
            cJSON_AddStringToObject(item, "note", p->note);
        } else {
            cJSON_AddNullToObject(item, "note");
        }
        cJSON_AddItemToArray(arr, item);
    }

    const char *json = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "[]");
    free((void *)json);
    cJSON_Delete(arr);
    return ESP_OK;
}

static bool check_auth(httpd_req_t *req)
{
    if (!auth_manager_is_enabled()) {
        return true;
    }
    char auth_header[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        return false;
    }
    return auth_manager_check_header(auth_header);
}

// Zentraler Einstiegspunkt fuer JEDE registrierte Route (siehe register_route()):
// erzwingt HTTP-Basic-Auth (falls aktiviert), BEVOR der eigentliche Handler
// aufgerufen wird. So kann kein neuer Endpoint versehentlich ungeschuetzt
// bleiben, weil die Pruefung in jedem einzelnen Handler wiederholt werden
// muesste.
static esp_err_t auth_trampoline(httpd_req_t *req)
{
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"TemperaturWatch\"");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    const route_ctx_t *ctx = (const route_ctx_t *)req->user_ctx;
    return ctx->handler(req);
}

static void register_route(httpd_handle_t server, const char *uri, httpd_method_t method,
                             esp_err_t (*handler)(httpd_req_t *))
{
    if (s_route_ctx_count >= MAX_ROUTES) {
        ESP_LOGE(TAG, "MAX_ROUTES ueberschritten, Route \"%s\" nicht registriert", uri);
        return;
    }
    route_ctx_t *ctx = &s_route_ctx[s_route_ctx_count++];
    ctx->handler = handler;
    const httpd_uri_t uri_def = {
        .uri = uri, .method = method, .handler = auth_trampoline, .user_ctx = ctx,
    };
    httpd_register_uri_handler(server, &uri_def);
}

esp_err_t rest_api_start(void)
{
    esp_err_t err = mount_www();
    if (err != ESP_OK) {
        return err;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 30;

    httpd_handle_t server = NULL;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    register_route(server, "/api/system/info", HTTP_GET, api_system_info_get_handler);
    register_route(server, "/api/board/pins", HTTP_GET, api_board_pins_get_handler);

    register_route(server, "/api/sensors", HTTP_GET, api_sensors_get_handler);
    register_route(server, "/api/sensors/config", HTTP_GET, api_sensors_config_get_handler);
    register_route(server, "/api/sensors/config", HTTP_PUT, api_sensors_config_put_handler);
    register_route(server, "/api/sensors/dallas-scan", HTTP_GET, api_sensors_dallas_scan_get_handler);

    register_route(server, "/api/io", HTTP_GET, api_io_get_handler);
    register_route(server, "/api/io/config", HTTP_GET, api_io_config_get_handler);
    register_route(server, "/api/io/config", HTTP_PUT, api_io_config_put_handler);
    register_route(server, "/api/io/set", HTTP_POST, api_io_set_post_handler);

    register_route(server, "/api/network/config", HTTP_GET, api_network_config_get_handler);
    register_route(server, "/api/network/config", HTTP_PUT, api_network_config_put_handler);

    register_route(server, "/api/mqtt/config", HTTP_GET, api_mqtt_config_get_handler);
    register_route(server, "/api/mqtt/config", HTTP_PUT, api_mqtt_config_put_handler);

    register_route(server, "/api/snmp/config", HTTP_GET, api_snmp_config_get_handler);
    register_route(server, "/api/snmp/config", HTTP_PUT, api_snmp_config_put_handler);

    register_route(server, "/api/auth/config", HTTP_GET, api_auth_config_get_handler);
    register_route(server, "/api/auth/config", HTTP_PUT, api_auth_config_put_handler);

    register_route(server, "/api/time/config", HTTP_GET, api_time_config_get_handler);
    register_route(server, "/api/time/config", HTTP_PUT, api_time_config_put_handler);

    register_route(server, "/api/system/reboot", HTTP_POST, api_system_reboot_post_handler);
    register_route(server, "/api/system/factory-reset", HTTP_POST, api_system_factory_reset_post_handler);
    register_route(server, "/api/system/ota", HTTP_POST, api_system_ota_post_handler);
    register_route(server, "/api/system/ota-web", HTTP_POST, api_system_ota_www_post_handler);

    // Muss zuletzt registriert werden: faengt alles ausser den obigen
    // /api/...-Routen als statische Datei aus SPIFFS ab.
    register_route(server, "/*", HTTP_GET, static_file_get_handler);

    ESP_LOGI(TAG, "REST-API/Web-UI-Server gestartet");
    return ESP_OK;
}
