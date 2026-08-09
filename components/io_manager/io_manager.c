#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "config_store.h"
#include "gpio_registry.h"
#include "sensor_manager.h"
#include "io_manager.h"

static const char *TAG = "io_manager";
static const char *OWNER_TAG = "io_manager";

typedef enum {
    IO_TYPE_OUTPUT,
    IO_TYPE_INPUT,
} io_type_t;

typedef enum {
    IO_OP_GT,
    IO_OP_GTE,
    IO_OP_LT,
    IO_OP_LTE,
} io_op_t;

typedef struct {
    bool enabled;
    char sensor_id[32];
    char field[16];
    io_op_t op;
    float threshold;
    float hysteresis; // >= 0, Totzone um den Schwellwert gegen schnelles Ein-/Ausschalten
} io_rule_t;

typedef struct {
    char id[32];
    io_type_t type;
    int gpio;
    char label[48];
    bool invert;
    bool initial_state; // nur output
    io_rule_t rule;      // nur output

    bool state; // logischer (post-invert) Zustand
} io_runtime_t;

static io_runtime_t s_ios[IO_MANAGER_MAX_ENTRIES];
static size_t s_io_count;
static SemaphoreHandle_t s_lock;

static const char *type_to_string(io_type_t type)
{
    return (type == IO_TYPE_OUTPUT) ? "output" : "input";
}

static bool parse_type(const char *s, io_type_t *out)
{
    if (strcmp(s, "output") == 0) { *out = IO_TYPE_OUTPUT; return true; }
    if (strcmp(s, "input") == 0)  { *out = IO_TYPE_INPUT;  return true; }
    return false;
}

static const char *op_to_string(io_op_t op)
{
    switch (op) {
    case IO_OP_GT:  return "gt";
    case IO_OP_GTE: return "gte";
    case IO_OP_LT:  return "lt";
    case IO_OP_LTE: return "lte";
    default: return "gt";
    }
}

static bool parse_op(const char *s, io_op_t *out)
{
    if (strcmp(s, "gt") == 0)  { *out = IO_OP_GT;  return true; }
    if (strcmp(s, "gte") == 0) { *out = IO_OP_GTE; return true; }
    if (strcmp(s, "lt") == 0)  { *out = IO_OP_LT;  return true; }
    if (strcmp(s, "lte") == 0) { *out = IO_OP_LTE; return true; }
    return false;
}

// Wertet die Regel mit Hysterese (Schmitt-Trigger-Verhalten) aus, um
// schnelles Ein-/Ausschalten nahe am Schwellwert zu vermeiden:
// - "gt"/"gte" (z.B. Kuehlung: an bei Ueberschreitung): schaltet EIN bei
//   value>threshold, aber erst wieder AUS, wenn value um `hysteresis` UNTER
//   den Schwellwert faellt.
// - "lt"/"lte" (z.B. Heizung: an bei Unterschreitung): spiegelbildlich,
//   schaltet erst wieder AUS, wenn value um `hysteresis` UEBER den
//   Schwellwert steigt.
// Bei hysteresis=0 verhaelt es sich identisch zum reinen Schwellwertvergleich.
static bool evaluate_rule(const io_rule_t *rule, float value, bool current_state)
{
    float h = rule->hysteresis > 0 ? rule->hysteresis : 0;
    switch (rule->op) {
    case IO_OP_GT:
    case IO_OP_GTE: {
        float off_point = rule->threshold - h;
        float compare = current_state ? off_point : rule->threshold;
        return (rule->op == IO_OP_GT) ? (value > compare) : (value >= compare);
    }
    case IO_OP_LT:
    case IO_OP_LTE: {
        float off_point = rule->threshold + h;
        float compare = current_state ? off_point : rule->threshold;
        return (rule->op == IO_OP_LT) ? (value < compare) : (value <= compare);
    }
    default:
        return false;
    }
}

// Parst das optionale "rule"-Objekt eines Konfig-Eintrags. Liefert false bei
// strukturell ungueltigem (aber vorhandenem) rule-Objekt.
static bool parse_rule(const cJSON *item, io_rule_t *out_rule)
{
    memset(out_rule, 0, sizeof(*out_rule));
    const cJSON *jrule = cJSON_GetObjectItem(item, "rule");
    if (jrule == NULL || cJSON_IsNull(jrule)) {
        return true; // keine Regel ist gueltig
    }
    const cJSON *jsensor = cJSON_GetObjectItem(jrule, "sensor_id");
    const cJSON *jfield = cJSON_GetObjectItem(jrule, "field");
    const cJSON *jop = cJSON_GetObjectItem(jrule, "operator");
    const cJSON *jthreshold = cJSON_GetObjectItem(jrule, "threshold");
    const cJSON *jhysteresis = cJSON_GetObjectItem(jrule, "hysteresis");
    if (!cJSON_IsString(jsensor) || !cJSON_IsString(jfield) || !cJSON_IsString(jop) ||
        !cJSON_IsNumber(jthreshold)) {
        return false;
    }
    if (strcmp(jfield->valuestring, "temperature_c") != 0 && strcmp(jfield->valuestring, "humidity_pct") != 0) {
        return false;
    }
    io_op_t op;
    if (!parse_op(jop->valuestring, &op)) {
        return false;
    }
    if (jhysteresis != NULL && !cJSON_IsNull(jhysteresis) &&
        (!cJSON_IsNumber(jhysteresis) || jhysteresis->valuedouble < 0)) {
        return false; // Hysterese muss, falls angegeben, eine Zahl >= 0 sein
    }
    out_rule->enabled = true;
    strlcpy(out_rule->sensor_id, jsensor->valuestring, sizeof(out_rule->sensor_id));
    strlcpy(out_rule->field, jfield->valuestring, sizeof(out_rule->field));
    out_rule->op = op;
    out_rule->threshold = (float)jthreshold->valuedouble;
    out_rule->hysteresis = (jhysteresis != NULL && cJSON_IsNumber(jhysteresis))
                                ? (float)jhysteresis->valuedouble : 0.0f;
    return true;
}

// Konfiguriert den GPIO fuer `io` passend zu Typ/Invert und setzt bei
// Ausgaengen den Startzustand.
static void apply_gpio_config(io_runtime_t *io)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << io->gpio,
        .mode = (io->type == IO_TYPE_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en = (io->type == IO_TYPE_INPUT) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };
    gpio_config(&cfg);

    if (io->type == IO_TYPE_OUTPUT) {
        io->state = io->initial_state;
        gpio_set_level(io->gpio, io->invert ? !io->state : io->state);
    }
}

// Muss mit s_lock gehalten aufgerufen werden.
static esp_err_t load_config_locked(void)
{
    s_io_count = 0;

    cJSON *arr = NULL;
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_IO, &arr);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        gpio_registry_release_owner(OWNER_TAG);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && s_io_count < IO_MANAGER_MAX_ENTRIES; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        const cJSON *jid = cJSON_GetObjectItem(item, "id");
        const cJSON *jtype = cJSON_GetObjectItem(item, "type");
        const cJSON *jgpio = cJSON_GetObjectItem(item, "gpio");
        const cJSON *jlabel = cJSON_GetObjectItem(item, "label");
        const cJSON *jinvert = cJSON_GetObjectItem(item, "invert");
        const cJSON *jinitial = cJSON_GetObjectItem(item, "initial_state");

        if (!cJSON_IsString(jid) || !cJSON_IsString(jtype) || !cJSON_IsNumber(jgpio)) {
            ESP_LOGW(TAG, "IO-Eintrag #%d: Pflichtfelder fehlen, wird uebersprungen", i);
            continue;
        }
        io_type_t type;
        if (!parse_type(jtype->valuestring, &type)) {
            ESP_LOGW(TAG, "IO \"%s\": unbekannter Typ \"%s\", wird uebersprungen",
                      jid->valuestring, jtype->valuestring);
            continue;
        }
        int gpio = jgpio->valueint;
        if (!board_pins_is_header_gpio(gpio)) {
            ESP_LOGW(TAG, "IO \"%s\": GPIO%d liegt nicht auf der Stiftleiste, wird uebersprungen",
                      jid->valuestring, gpio);
            continue;
        }
        const char *owner = gpio_registry_owner(gpio);
        if (owner != NULL && strcmp(owner, OWNER_TAG) != 0) {
            ESP_LOGW(TAG, "IO \"%s\": GPIO%d ist bereits durch \"%s\" belegt, wird uebersprungen",
                      jid->valuestring, gpio, owner);
            continue;
        }
        io_rule_t rule;
        if (!parse_rule(item, &rule)) {
            ESP_LOGW(TAG, "IO \"%s\": ungueltige Schwellwert-Regel, wird uebersprungen", jid->valuestring);
            continue;
        }

        io_runtime_t *io = &s_ios[s_io_count];
        memset(io, 0, sizeof(*io));
        strlcpy(io->id, jid->valuestring, sizeof(io->id));
        io->type = type;
        io->gpio = gpio;
        if (cJSON_IsString(jlabel)) {
            strlcpy(io->label, jlabel->valuestring, sizeof(io->label));
        }
        io->invert = cJSON_IsBool(jinvert) && cJSON_IsTrue(jinvert);
        io->initial_state = cJSON_IsBool(jinitial) && cJSON_IsTrue(jinitial);
        if (type == IO_TYPE_OUTPUT) {
            io->rule = rule;
        }

        apply_gpio_config(io);
        s_io_count++;
    }

    cJSON_Delete(arr);

    gpio_registry_release_owner(OWNER_TAG);
    for (size_t i = 0; i < s_io_count; i++) {
        gpio_registry_claim(s_ios[i].gpio, OWNER_TAG);
    }

    ESP_LOGI(TAG, "%u IO(s) aus Config geladen", (unsigned)s_io_count);
    return ESP_OK;
}

// Prueft GPIO-Eindeutigkeit (auch gegen andere Owner im gpio_registry) und
// Feldgueltigkeit, OHNE Laufzeitzustand zu veraendern.
static bool validate_config(const cJSON *array)
{
    if (!cJSON_IsArray(array)) {
        return false;
    }

    int gpios[IO_MANAGER_MAX_ENTRIES];
    int n = 0;

    const cJSON *item;
    cJSON_ArrayForEach(item, array) {
        const cJSON *jid = cJSON_GetObjectItem(item, "id");
        const cJSON *jtype = cJSON_GetObjectItem(item, "type");
        const cJSON *jgpio = cJSON_GetObjectItem(item, "gpio");
        if (!cJSON_IsString(jid) || !cJSON_IsString(jtype) || !cJSON_IsNumber(jgpio)) {
            return false;
        }
        io_type_t type;
        if (!parse_type(jtype->valuestring, &type)) {
            return false;
        }
        int gpio = jgpio->valueint;
        if (!board_pins_is_header_gpio(gpio)) {
            return false;
        }
        const char *owner = gpio_registry_owner(gpio);
        if (owner != NULL && strcmp(owner, OWNER_TAG) != 0) {
            return false;
        }
        io_rule_t rule;
        if (!parse_rule(item, &rule)) {
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (gpios[i] == gpio) {
                return false; // jeder GPIO nur fuer genau einen IO-Eintrag
            }
        }
        if (n < IO_MANAGER_MAX_ENTRIES) {
            gpios[n++] = gpio;
        }
    }
    return true;
}

static void io_tick(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_io_count; i++) {
        io_runtime_t *io = &s_ios[i];

        if (io->type == IO_TYPE_INPUT) {
            int level = gpio_get_level(io->gpio);
            io->state = io->invert ? !level : (level != 0);
            continue;
        }

        if (!io->rule.enabled) {
            continue;
        }
        float value = 0.0f;
        bool valid = false;
        esp_err_t err = sensor_manager_get_value(io->rule.sensor_id, io->rule.field, &value, &valid);
        if (err != ESP_OK || !valid) {
            continue; // Sensor (noch) ohne gueltigen Wert - Ausgang unveraendert lassen
        }
        bool should_be_on = evaluate_rule(&io->rule, value, io->state);
        if (should_be_on != io->state) {
            io->state = should_be_on;
            gpio_set_level(io->gpio, io->invert ? !io->state : io->state);
        }
    }
    xSemaphoreGive(s_lock);
}

static void io_task(void *arg)
{
    while (1) {
        io_tick();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t io_manager_start(void)
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

    BaseType_t ok = xTaskCreate(io_task, "io_task", 3072, NULL, tskIDLE_PRIORITY + 2, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t io_manager_get_states_json(cJSON **out_array)
{
    if (out_array == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *arr = cJSON_CreateArray();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_io_count; i++) {
        io_runtime_t *io = &s_ios[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", io->id);
        cJSON_AddStringToObject(item, "type", type_to_string(io->type));
        cJSON_AddNumberToObject(item, "gpio", io->gpio);
        cJSON_AddStringToObject(item, "label", io->label);
        cJSON_AddBoolToObject(item, "invert", io->invert);
        cJSON_AddBoolToObject(item, "state", io->state);
        if (io->type == IO_TYPE_OUTPUT && io->rule.enabled) {
            cJSON *rule = cJSON_CreateObject();
            cJSON_AddStringToObject(rule, "sensor_id", io->rule.sensor_id);
            cJSON_AddStringToObject(rule, "field", io->rule.field);
            cJSON_AddStringToObject(rule, "operator", op_to_string(io->rule.op));
            cJSON_AddNumberToObject(rule, "threshold", io->rule.threshold);
            cJSON_AddNumberToObject(rule, "hysteresis", io->rule.hysteresis);
            cJSON_AddItemToObject(item, "rule", rule);
        }
        cJSON_AddItemToArray(arr, item);
    }
    xSemaphoreGive(s_lock);

    *out_array = arr;
    return ESP_OK;
}

esp_err_t io_manager_get_config_json(cJSON **out_array)
{
    if (out_array == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_IO, out_array);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_array = cJSON_CreateArray();
        return ESP_OK;
    }
    return err;
}

esp_err_t io_manager_set_config_json(const cJSON *array)
{
    if (!validate_config(array)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_IO, array);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    err = load_config_locked();
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t io_manager_set_output(const char *id, bool state)
{
    if (id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < s_io_count; i++) {
        io_runtime_t *io = &s_ios[i];
        if (strcmp(io->id, id) != 0 || io->type != IO_TYPE_OUTPUT) {
            continue;
        }
        if (io->rule.enabled) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        io->state = state;
        gpio_set_level(io->gpio, io->invert ? !io->state : io->state);
        err = ESP_OK;
        break;
    }
    xSemaphoreGive(s_lock);
    return err;
}
