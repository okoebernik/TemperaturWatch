#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "config_store.h"
#include "io_manager.h"
#include "sensor_manager.h"
#include "mqtt_manager.h"

static const char *TAG = "mqtt_manager";

typedef struct {
    bool enabled;
    char broker_uri[128];
    char username[64];
    char password[64];
    char client_id[32];
    char base_topic[32];
    uint32_t publish_interval_s;
    char *ca_cert; // heap (strdup), NULL wenn keins - muss fuer die Lebensdauer
                    // des Clients gueltig bleiben (esp-mqtt kopiert ihn nicht).
} mqtt_config_t;

static mqtt_config_t s_config;
static esp_mqtt_client_handle_t s_client;
static SemaphoreHandle_t s_lock;
static volatile bool s_connected;

// Baut ein JSON-Objekt aus `cfg`. Bei mask_password=true wird das
// Passwort-Feld als leerer String zurueckgegeben (fuer REST-Antworten);
// mask_password=false liefert den echten Wert (fuer die NVS-Persistenz).
static cJSON *config_to_json(const mqtt_config_t *cfg, bool mask_password)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", cfg->enabled);
    cJSON_AddStringToObject(o, "broker_uri", cfg->broker_uri);
    cJSON_AddStringToObject(o, "username", cfg->username);
    cJSON_AddStringToObject(o, "password", mask_password ? "" : cfg->password);
    cJSON_AddStringToObject(o, "client_id", cfg->client_id);
    cJSON_AddStringToObject(o, "base_topic", cfg->base_topic);
    cJSON_AddNumberToObject(o, "publish_interval_s", cfg->publish_interval_s);
    cJSON_AddStringToObject(o, "ca_cert", cfg->ca_cert != NULL ? cfg->ca_cert : "");
    return o;
}

// `previous_password` wird verwendet, wenn `json` kein (oder ein leeres)
// "password"-Feld enthaelt - so muss die REST-Antwort das Passwort nicht
// jedes Mal im Klartext zurueckliefern, um es beim naechsten Speichern
// beizubehalten. Bei Erfolg zeigt out->ca_cert ggf. auf neu alloziertem
// Speicher (Aufrufer verantwortlich fuer free()).
static bool parse_config(const cJSON *json, const char *previous_password, mqtt_config_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(json)) {
        return false;
    }

    const cJSON *jenabled = cJSON_GetObjectItem(json, "enabled");
    out->enabled = cJSON_IsBool(jenabled) && cJSON_IsTrue(jenabled);

    const cJSON *jbroker = cJSON_GetObjectItem(json, "broker_uri");
    if (cJSON_IsString(jbroker)) {
        strlcpy(out->broker_uri, jbroker->valuestring, sizeof(out->broker_uri));
    }
    if (out->enabled) {
        if (strlen(out->broker_uri) == 0) {
            return false;
        }
        if (strncmp(out->broker_uri, "mqtt://", 7) != 0 && strncmp(out->broker_uri, "mqtts://", 8) != 0) {
            return false;
        }
    }

    const cJSON *jusername = cJSON_GetObjectItem(json, "username");
    if (cJSON_IsString(jusername)) {
        strlcpy(out->username, jusername->valuestring, sizeof(out->username));
    }

    const cJSON *jpassword = cJSON_GetObjectItem(json, "password");
    if (cJSON_IsString(jpassword) && strlen(jpassword->valuestring) > 0) {
        strlcpy(out->password, jpassword->valuestring, sizeof(out->password));
    } else if (previous_password != NULL) {
        strlcpy(out->password, previous_password, sizeof(out->password));
    }

    const cJSON *jclientid = cJSON_GetObjectItem(json, "client_id");
    if (cJSON_IsString(jclientid)) {
        strlcpy(out->client_id, jclientid->valuestring, sizeof(out->client_id));
    }

    const cJSON *jbase = cJSON_GetObjectItem(json, "base_topic");
    if (cJSON_IsString(jbase) && strlen(jbase->valuestring) > 0) {
        strlcpy(out->base_topic, jbase->valuestring, sizeof(out->base_topic));
    } else {
        strlcpy(out->base_topic, "temperaturwatch", sizeof(out->base_topic));
    }

    const cJSON *jinterval = cJSON_GetObjectItem(json, "publish_interval_s");
    uint32_t interval = cJSON_IsNumber(jinterval) ? (uint32_t)jinterval->valueint : 30;
    out->publish_interval_s = interval < 5 ? 5 : interval;

    const cJSON *jca = cJSON_GetObjectItem(json, "ca_cert");
    if (cJSON_IsString(jca) && strlen(jca->valuestring) > 0) {
        out->ca_cert = strdup(jca->valuestring);
    }

    return true;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_connected = true;
        char topic[64];
        snprintf(topic, sizeof(topic), "%s/io/+/set", s_config.base_topic);
        esp_mqtt_client_subscribe(event->client, topic, 1);
        char status_topic[48];
        snprintf(status_topic, sizeof(status_topic), "%s/status", s_config.base_topic);
        esp_mqtt_client_publish(event->client, status_topic, "online", 0, 1, 1);
        ESP_LOGI(TAG, "MQTT verbunden (Broker: %s)", s_config.broker_uri);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT-Verbindung getrennt");
        break;
    case MQTT_EVENT_DATA: {
        char topic_buf[64];
        char payload_buf[16];
        int tlen = event->topic_len < (int)sizeof(topic_buf) - 1 ? event->topic_len : (int)sizeof(topic_buf) - 1;
        memcpy(topic_buf, event->topic, tlen);
        topic_buf[tlen] = '\0';
        int plen = event->data_len < (int)sizeof(payload_buf) - 1 ? event->data_len : (int)sizeof(payload_buf) - 1;
        memcpy(payload_buf, event->data, plen);
        payload_buf[plen] = '\0';

        char prefix[48];
        snprintf(prefix, sizeof(prefix), "%s/io/", s_config.base_topic);
        size_t prefix_len = strlen(prefix);
        size_t suffix_len = strlen("/set");
        size_t topic_len = strlen(topic_buf);
        if (topic_len > prefix_len + suffix_len &&
            strncmp(topic_buf, prefix, prefix_len) == 0 &&
            strcmp(topic_buf + topic_len - suffix_len, "/set") == 0) {
            char id[32];
            size_t id_len = topic_len - prefix_len - suffix_len;
            if (id_len < sizeof(id)) {
                memcpy(id, topic_buf + prefix_len, id_len);
                id[id_len] = '\0';
                bool state = (strcasecmp(payload_buf, "on") == 0 || strcasecmp(payload_buf, "true") == 0 ||
                              strcmp(payload_buf, "1") == 0);
                esp_err_t err = io_manager_set_output(id, state);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "MQTT IO-Steuerung \"%s\": %s", id, esp_err_to_name(err));
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

// Muss mit s_lock gehalten aufgerufen werden.
static void stop_client_locked(void)
{
    if (s_client != NULL) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
}

// Muss mit s_lock gehalten aufgerufen werden.
static esp_err_t start_client_locked(void)
{
    if (!s_config.enabled) {
        return ESP_OK;
    }

    char lwt_topic[48];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", s_config.base_topic);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = s_config.broker_uri,
        .credentials.username = strlen(s_config.username) > 0 ? s_config.username : NULL,
        .credentials.authentication.password = strlen(s_config.password) > 0 ? s_config.password : NULL,
        .credentials.client_id = strlen(s_config.client_id) > 0 ? s_config.client_id : NULL,
        .session.keepalive = 60,
        .session.last_will.topic = lwt_topic,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
    };
    if (s_config.ca_cert != NULL) {
        mqtt_cfg.broker.verification.certificate = s_config.ca_cert;
        mqtt_cfg.broker.verification.certificate_len = strlen(s_config.ca_cert) + 1;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init fehlgeschlagen");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start fehlgeschlagen: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    ESP_LOGI(TAG, "MQTT-Client gestartet, Broker: %s", s_config.broker_uri);
    return ESP_OK;
}

// Muss mit s_lock gehalten aufgerufen werden. Ersetzt s_config komplett
// durch `new_cfg` (uebernimmt Ownership von new_cfg->ca_cert) und
// (re)startet den Client passend zur neuen Konfiguration.
static esp_err_t apply_config_locked(mqtt_config_t *new_cfg)
{
    stop_client_locked();
    free(s_config.ca_cert);
    s_config = *new_cfg;
    return start_client_locked();
}

// Muss mit s_lock gehalten aufgerufen werden.
static esp_err_t load_config_locked(void)
{
    cJSON *json = NULL;
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_MQTT, &json);

    mqtt_config_t new_cfg;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&new_cfg, 0, sizeof(new_cfg));
        strlcpy(new_cfg.base_topic, "temperaturwatch", sizeof(new_cfg.base_topic));
        new_cfg.publish_interval_s = 30;
    } else if (err != ESP_OK) {
        return err;
    } else {
        bool ok = parse_config(json, NULL, &new_cfg);
        cJSON_Delete(json);
        if (!ok) {
            ESP_LOGW(TAG, "Gespeicherte MQTT-Konfiguration ist ungueltig, verwende Defaults");
            memset(&new_cfg, 0, sizeof(new_cfg));
            strlcpy(new_cfg.base_topic, "temperaturwatch", sizeof(new_cfg.base_topic));
            new_cfg.publish_interval_s = 30;
        }
    }

    return apply_config_locked(&new_cfg);
}

static void publish_readings_locked(void)
{
    if (!s_connected || s_client == NULL) {
        return;
    }

    cJSON *sensors = NULL;
    sensor_manager_get_readings_json(&sensors);
    const cJSON *item;
    cJSON_ArrayForEach(item, sensors) {
        const cJSON *jid = cJSON_GetObjectItem(item, "id");
        const cJSON *jok = cJSON_GetObjectItem(item, "last_read_ok");
        if (!cJSON_IsString(jid) || !cJSON_IsTrue(jok)) {
            continue;
        }
        const cJSON *jtemp = cJSON_GetObjectItem(item, "temperature_c");
        const cJSON *jhum = cJSON_GetObjectItem(item, "humidity_pct");
        char topic[80];
        char payload[16];
        if (cJSON_IsNumber(jtemp)) {
            snprintf(topic, sizeof(topic), "%s/sensor/%s/temperature_c", s_config.base_topic, jid->valuestring);
            snprintf(payload, sizeof(payload), "%.2f", jtemp->valuedouble);
            esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
        }
        if (cJSON_IsNumber(jhum)) {
            snprintf(topic, sizeof(topic), "%s/sensor/%s/humidity_pct", s_config.base_topic, jid->valuestring);
            snprintf(payload, sizeof(payload), "%.2f", jhum->valuedouble);
            esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
        }
    }
    cJSON_Delete(sensors);

    cJSON *ios = NULL;
    io_manager_get_states_json(&ios);
    cJSON_ArrayForEach(item, ios) {
        const cJSON *jid = cJSON_GetObjectItem(item, "id");
        const cJSON *jstate = cJSON_GetObjectItem(item, "state");
        if (!cJSON_IsString(jid) || !cJSON_IsBool(jstate)) {
            continue;
        }
        char topic[80];
        snprintf(topic, sizeof(topic), "%s/io/%s/state", s_config.base_topic, jid->valuestring);
        esp_mqtt_client_publish(s_client, topic, cJSON_IsTrue(jstate) ? "ON" : "OFF", 0, 0, 1);
    }
    cJSON_Delete(ios);
}

static void mqtt_task(void *arg)
{
    int64_t next_publish = 0;
    while (1) {
        int64_t now = esp_timer_get_time();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_config.enabled && now >= next_publish) {
            publish_readings_locked();
            next_publish = now + (int64_t)s_config.publish_interval_s * 1000000;
        }
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t mqtt_manager_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = load_config_locked();
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT-Start fehlgeschlagen: %s", esp_err_to_name(err));
    }

    BaseType_t ok = xTaskCreate(mqtt_task, "mqtt_task", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t mqtt_manager_get_config_json(cJSON **out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out_config = config_to_json(&s_config, true);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t mqtt_manager_set_config_json(const cJSON *config)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    mqtt_config_t new_cfg;
    if (!parse_config(config, s_config.password, &new_cfg)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *to_store = config_to_json(&new_cfg, false);
    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_MQTT, to_store);
    cJSON_Delete(to_store);
    if (err != ESP_OK) {
        free(new_cfg.ca_cert);
        xSemaphoreGive(s_lock);
        return err;
    }

    err = apply_config_locked(&new_cfg);
    xSemaphoreGive(s_lock);
    return err;
}

bool mqtt_manager_is_connected(void)
{
    return s_connected;
}

bool mqtt_manager_is_enabled(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_config.enabled;
    xSemaphoreGive(s_lock);
    return enabled;
}
