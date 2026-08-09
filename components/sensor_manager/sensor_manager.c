#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "config_store.h"
#include "dallas_sensor.h"
#include "dht_sensor.h"
#include "gpio_registry.h"
#include "sensor_manager.h"

static const char *TAG = "sensor_manager";
static const char *OWNER_TAG = "sensor_manager";

typedef enum {
    SENSOR_TYPE_DALLAS,
    SENSOR_TYPE_DHT11,
    SENSOR_TYPE_AM2301,
} sensor_type_t;

typedef struct {
    char id[32];
    sensor_type_t type;
    int gpio;
    uint64_t rom_id; // nur Dallas; 0 = "erstes gefundenes Geraet auf dem Bus"
    char label[48];
    uint32_t poll_interval_s;

    bool has_reading;
    bool last_read_ok;
    float temperature_c;
    float humidity_pct;
    bool has_humidity;
    int64_t last_read_time_us;
    int64_t next_poll_time_us;
} sensor_runtime_t;

static sensor_runtime_t s_sensors[SENSOR_MANAGER_MAX_SENSORS];
static size_t s_sensor_count;
static SemaphoreHandle_t s_lock;

static const char *type_to_string(sensor_type_t type)
{
    switch (type) {
    case SENSOR_TYPE_DALLAS: return "dallas";
    case SENSOR_TYPE_DHT11:  return "dht11";
    case SENSOR_TYPE_AM2301: return "am2301";
    default: return "unknown";
    }
}

static bool parse_type(const char *s, sensor_type_t *out)
{
    if (strcmp(s, "dallas") == 0) { *out = SENSOR_TYPE_DALLAS; return true; }
    if (strcmp(s, "dht11") == 0)  { *out = SENSOR_TYPE_DHT11;  return true; }
    if (strcmp(s, "am2301") == 0) { *out = SENSOR_TYPE_AM2301; return true; }
    return false;
}

// Muss mit s_lock gehalten aufgerufen werden.
static esp_err_t load_config_locked(void)
{
    s_sensor_count = 0;

    cJSON *arr = NULL;
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_SENSORS, &arr);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; // keine Konfiguration = leere Sensor-Liste, kein Fehler
    }
    if (err != ESP_OK) {
        return err;
    }

    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && s_sensor_count < SENSOR_MANAGER_MAX_SENSORS; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        const cJSON *jid = cJSON_GetObjectItem(item, "id");
        const cJSON *jtype = cJSON_GetObjectItem(item, "type");
        const cJSON *jgpio = cJSON_GetObjectItem(item, "gpio");
        const cJSON *jlabel = cJSON_GetObjectItem(item, "label");
        const cJSON *jinterval = cJSON_GetObjectItem(item, "poll_interval_s");
        const cJSON *jrom = cJSON_GetObjectItem(item, "rom_id");

        if (!cJSON_IsString(jid) || !cJSON_IsString(jtype) || !cJSON_IsNumber(jgpio)) {
            ESP_LOGW(TAG, "Sensor-Eintrag #%d: Pflichtfelder fehlen, wird uebersprungen", i);
            continue;
        }
        sensor_type_t type;
        if (!parse_type(jtype->valuestring, &type)) {
            ESP_LOGW(TAG, "Sensor \"%s\": unbekannter Typ \"%s\", wird uebersprungen",
                      jid->valuestring, jtype->valuestring);
            continue;
        }
        int gpio = jgpio->valueint;
        if (!board_pins_is_header_gpio(gpio)) {
            ESP_LOGW(TAG, "Sensor \"%s\": GPIO%d liegt nicht auf der Stiftleiste, wird uebersprungen",
                      jid->valuestring, gpio);
            continue;
        }

        sensor_runtime_t *s = &s_sensors[s_sensor_count];
        memset(s, 0, sizeof(*s));
        strlcpy(s->id, jid->valuestring, sizeof(s->id));
        s->type = type;
        s->gpio = gpio;
        if (cJSON_IsString(jlabel)) {
            strlcpy(s->label, jlabel->valuestring, sizeof(s->label));
        }
        s->poll_interval_s = (cJSON_IsNumber(jinterval) && jinterval->valueint > 0)
                                  ? (uint32_t)jinterval->valueint : 10;
        if (type == SENSOR_TYPE_DALLAS && cJSON_IsString(jrom) && strlen(jrom->valuestring) > 0) {
            s->rom_id = strtoull(jrom->valuestring, NULL, 16);
        }
        s->next_poll_time_us = 0; // beim naechsten Tick sofort lesen
        s_sensor_count++;
    }

    cJSON_Delete(arr);

    gpio_registry_release_owner(OWNER_TAG);
    for (size_t i = 0; i < s_sensor_count; i++) {
        gpio_registry_claim(s_sensors[i].gpio, OWNER_TAG);
    }

    ESP_LOGI(TAG, "%u Sensor(en) aus Config geladen", (unsigned)s_sensor_count);
    return ESP_OK;
}

// Prueft GPIO-Zuweisungen auf Konflikte: Ein GPIO darf von mehreren
// Dallas-Sensoren geteilt werden (Multi-Drop-Bus), aber nicht von zwei
// DHT-Sensoren oder gemischt Dallas+DHT.
static bool validate_config(const cJSON *array)
{
    if (!cJSON_IsArray(array)) {
        return false;
    }

    int gpios[SENSOR_MANAGER_MAX_SENSORS];
    sensor_type_t types[SENSOR_MANAGER_MAX_SENSORS];
    int n = 0;

    const cJSON *item;
    cJSON_ArrayForEach(item, array) {
        const cJSON *jid = cJSON_GetObjectItem(item, "id");
        const cJSON *jtype = cJSON_GetObjectItem(item, "type");
        const cJSON *jgpio = cJSON_GetObjectItem(item, "gpio");
        if (!cJSON_IsString(jid) || !cJSON_IsString(jtype) || !cJSON_IsNumber(jgpio)) {
            return false;
        }
        sensor_type_t type;
        if (!parse_type(jtype->valuestring, &type)) {
            return false;
        }
        int gpio = jgpio->valueint;
        if (!board_pins_is_header_gpio(gpio)) {
            return false;
        }
        const char *owner = gpio_registry_owner(gpio);
        if (owner != NULL && strcmp(owner, OWNER_TAG) != 0) {
            ESP_LOGW(TAG, "GPIO%d ist bereits durch \"%s\" belegt", gpio, owner);
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (gpios[i] == gpio && (type != SENSOR_TYPE_DALLAS || types[i] != SENSOR_TYPE_DALLAS)) {
                return false; // GPIO-Konflikt (kein Dallas-Multidrop)
            }
        }
        if (n < SENSOR_MANAGER_MAX_SENSORS) {
            gpios[n] = gpio;
            types[n] = type;
            n++;
        }
    }
    return true;
}

static void poll_tick(void)
{
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);

    // DHT-Sensoren: ein GPIO = ein Sensor, einzeln pollen.
    for (size_t i = 0; i < s_sensor_count; i++) {
        sensor_runtime_t *s = &s_sensors[i];
        if (s->type == SENSOR_TYPE_DALLAS || now < s->next_poll_time_us) {
            continue;
        }
        dht_reading_t r;
        dht_type_t dt = (s->type == SENSOR_TYPE_DHT11) ? DHT_TYPE_DHT11 : DHT_TYPE_AM2301;
        esp_err_t err = dht_read(s->gpio, dt, &r);
        s->has_reading = true;
        s->last_read_ok = (err == ESP_OK);
        if (err == ESP_OK) {
            s->temperature_c = r.temperature_c;
            s->humidity_pct = r.humidity_pct;
            s->has_humidity = true;
        } else {
            ESP_LOGW(TAG, "Sensor \"%s\" (GPIO%d): %s", s->id, s->gpio, esp_err_to_name(err));
        }
        s->last_read_time_us = now;
        s->next_poll_time_us = now + (int64_t)s->poll_interval_s * 1000000;
    }

    // Dallas-Sensoren: je faelligem GPIO nur einmal lesen (ein Bus kann
    // mehrere konfigurierte Sensoren bedienen), Ergebnisse per ROM-ID
    // (oder "erstes Geraet" bei rom_id==0) verteilen.
    int due_gpios[SENSOR_MANAGER_MAX_SENSORS];
    size_t due_gpio_count = 0;
    for (size_t i = 0; i < s_sensor_count; i++) {
        sensor_runtime_t *s = &s_sensors[i];
        if (s->type != SENSOR_TYPE_DALLAS || now < s->next_poll_time_us) {
            continue;
        }
        bool seen = false;
        for (size_t j = 0; j < due_gpio_count; j++) {
            if (due_gpios[j] == s->gpio) { seen = true; break; }
        }
        if (!seen && due_gpio_count < SENSOR_MANAGER_MAX_SENSORS) {
            due_gpios[due_gpio_count++] = s->gpio;
        }
    }

    for (size_t g = 0; g < due_gpio_count; g++) {
        dallas_reading_t readings[DALLAS_MAX_DEVICES_PER_BUS];
        size_t count = 0;
        esp_err_t err = dallas_read_bus(due_gpios[g], readings, DALLAS_MAX_DEVICES_PER_BUS, &count);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Dallas-Bus GPIO%d: %s", due_gpios[g], esp_err_to_name(err));
        }

        for (size_t i = 0; i < s_sensor_count; i++) {
            sensor_runtime_t *s = &s_sensors[i];
            if (s->type != SENSOR_TYPE_DALLAS || s->gpio != due_gpios[g] || now < s->next_poll_time_us) {
                continue;
            }
            s->has_reading = true;
            s->last_read_time_us = now;
            s->next_poll_time_us = now + (int64_t)s->poll_interval_s * 1000000;

            if (err != ESP_OK) {
                s->last_read_ok = false;
                continue;
            }
            s->last_read_ok = false;
            for (size_t k = 0; k < count; k++) {
                if (s->rom_id == 0 || s->rom_id == readings[k].rom_code) {
                    s->last_read_ok = readings[k].valid;
                    s->temperature_c = readings[k].temperature_c;
                    s->has_humidity = false;
                    break;
                }
            }
        }
    }

    xSemaphoreGive(s_lock);
}

static void sensor_task(void *arg)
{
    while (1) {
        poll_tick();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t sensor_manager_start(void)
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
        return err;
    }

    BaseType_t ok = xTaskCreate(sensor_task, "sensor_task", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t sensor_manager_get_readings_json(cJSON **out_array)
{
    if (out_array == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *arr = cJSON_CreateArray();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_sensor_count; i++) {
        sensor_runtime_t *s = &s_sensors[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", s->id);
        cJSON_AddStringToObject(item, "type", type_to_string(s->type));
        cJSON_AddNumberToObject(item, "gpio", s->gpio);
        cJSON_AddStringToObject(item, "label", s->label);
        cJSON_AddNumberToObject(item, "poll_interval_s", s->poll_interval_s);
        if (s->type == SENSOR_TYPE_DALLAS && s->rom_id != 0) {
            char rom_str[17];
            snprintf(rom_str, sizeof(rom_str), "%016" PRIX64, s->rom_id);
            cJSON_AddStringToObject(item, "rom_id", rom_str);
        }
        cJSON_AddBoolToObject(item, "has_reading", s->has_reading);
        cJSON_AddBoolToObject(item, "last_read_ok", s->last_read_ok);
        if (s->has_reading && s->last_read_ok) {
            cJSON_AddNumberToObject(item, "temperature_c", s->temperature_c);
            if (s->has_humidity) {
                cJSON_AddNumberToObject(item, "humidity_pct", s->humidity_pct);
            }
        }
        cJSON_AddNumberToObject(item, "last_read_time_ms", (double)(s->last_read_time_us / 1000));
        cJSON_AddItemToArray(arr, item);
    }
    xSemaphoreGive(s_lock);

    *out_array = arr;
    return ESP_OK;
}

esp_err_t sensor_manager_get_config_json(cJSON **out_array)
{
    if (out_array == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_SENSORS, out_array);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_array = cJSON_CreateArray();
        return ESP_OK;
    }
    return err;
}

esp_err_t sensor_manager_set_config_json(const cJSON *array)
{
    if (!validate_config(array)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_SENSORS, array);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    err = load_config_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t sensor_manager_get_value(const char *sensor_id, const char *field, float *out_value, bool *out_valid)
{
    if (sensor_id == NULL || field == NULL || out_value == NULL || out_valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_valid = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < s_sensor_count; i++) {
        sensor_runtime_t *s = &s_sensors[i];
        if (strcmp(s->id, sensor_id) != 0) {
            continue;
        }
        err = ESP_OK;
        if (s->has_reading && s->last_read_ok) {
            if (strcmp(field, "temperature_c") == 0) {
                *out_value = s->temperature_c;
                *out_valid = true;
            } else if (strcmp(field, "humidity_pct") == 0 && s->has_humidity) {
                *out_value = s->humidity_pct;
                *out_valid = true;
            }
        }
        break;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t sensor_manager_scan_dallas_bus_json(int gpio, cJSON **out_array)
{
    if (out_array == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!board_pins_is_header_gpio(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t roms[DALLAS_MAX_DEVICES_PER_BUS];
    size_t count = 0;
    esp_err_t err = dallas_scan_bus(gpio, roms, DALLAS_MAX_DEVICES_PER_BUS, &count);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *arr = cJSON_CreateArray();
    size_t n = count < DALLAS_MAX_DEVICES_PER_BUS ? count : DALLAS_MAX_DEVICES_PER_BUS;
    for (size_t i = 0; i < n; i++) {
        char rom_str[17];
        snprintf(rom_str, sizeof(rom_str), "%016" PRIX64, roms[i]);
        cJSON_AddItemToArray(arr, cJSON_CreateString(rom_str));
    }
    *out_array = arr;
    return ESP_OK;
}
