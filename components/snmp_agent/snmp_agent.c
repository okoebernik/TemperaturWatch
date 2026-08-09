#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "lwip/apps/snmp.h"
#include "lwip/apps/snmp_core.h"
#include "lwip/apps/snmp_scalar.h"
#include "lwip/apps/snmp_table.h"

#include "config_store.h"
#include "io_manager.h"
#include "sensor_manager.h"
#include "snmp_agent.h"

// Diese Datei implementiert eine EIGENE, schlanke SNMP-MIB direkt auf Basis
// von lwIP's SNMP-Protokoll-Engine (snmp_core/snmp_asn1/snmp_msg/snmp_raw/
// snmp_scalar/snmp_table - vendored aus components/lwip/lwip/src/apps/snmp,
// siehe CMakeLists.txt), NICHT lwIP's fertiges snmp_mib2-Modul.
//
// Grund: CONFIG_LWIP_SNMP existiert in dieser ESP-IDF-Version nicht als
// Kconfig-Symbol, und snmp_mib2*.c liest Zaehler aus dem globalen
// `lwip_stats`-Struct, dessen Speicherlayout vom MIB2_STATS-Flag abhaengt,
// mit dem der Rest von lwIP (das gemeinsame Framework-"lwip"-Component)
// bereits kompiliert wurde (MIB2_STATS=0). Wuerde man snmp_mib2*.c hier mit
// MIB2_STATS=1 dazukompilieren, entstuende ein ABI-Mismatch beim Zugriff auf
// das tatsaechlich (mit MIB2_STATS=0) angelegte globale `lwip_stats`-Objekt.
// Die Protokoll-Engine-Dateien selbst (snmp_core.c etc.) fassen dieses
// Struct nicht an und sind daher gefahrlos separat kompilierbar.
//
// Ergebnis: ein spezifikationskonformer SNMPv1/v2c-Agent (GET/GETNEXT) mit
// eigenem OID-Baum (System-Skalare + Sensor-/IO-Tabellen), aber OHNE die
// automatischen MIB-II-Paketzaehler (ifInOctets etc.), die eine Neukompilierung
// des gesamten lwIP-Cores erfordern wuerden.

static const char *TAG = "snmp_agent";

// Platzhalter-OID (Private Enterprise Number 99999 ist NICHT bei der IANA
// registriert). Bei Bedarf durch eine echte, bei IANA beantragte PEN
// ersetzen: https://www.iana.org/numbers.html
static const u32_t s_base_oid[] = { 1, 3, 6, 1, 4, 1, 99999, 1 };

typedef struct {
    bool enabled;
    char community[32];
    char sys_name[32];
    char sys_contact[32];
    char sys_location[32];
} snmp_config_t;

static snmp_config_t s_config;
static SemaphoreHandle_t s_lock;
static bool s_listener_started;

// ---------------------------------------------------------------------
// Konfiguration
// ---------------------------------------------------------------------

static bool parse_config(const cJSON *json, snmp_config_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(json)) {
        return false;
    }

    const cJSON *jenabled = cJSON_GetObjectItem(json, "enabled");
    out->enabled = cJSON_IsBool(jenabled) && cJSON_IsTrue(jenabled);

    const cJSON *jcommunity = cJSON_GetObjectItem(json, "community");
    const char *community = (cJSON_IsString(jcommunity) && strlen(jcommunity->valuestring) > 0)
                                 ? jcommunity->valuestring : "public";
    strlcpy(out->community, community, sizeof(out->community));

    const cJSON *jname = cJSON_GetObjectItem(json, "sys_name");
    if (cJSON_IsString(jname)) {
        strlcpy(out->sys_name, jname->valuestring, sizeof(out->sys_name));
    } else {
        strlcpy(out->sys_name, CONFIG_TW_HOSTNAME, sizeof(out->sys_name));
    }

    const cJSON *jcontact = cJSON_GetObjectItem(json, "sys_contact");
    if (cJSON_IsString(jcontact)) {
        strlcpy(out->sys_contact, jcontact->valuestring, sizeof(out->sys_contact));
    }

    const cJSON *jlocation = cJSON_GetObjectItem(json, "sys_location");
    if (cJSON_IsString(jlocation)) {
        strlcpy(out->sys_location, jlocation->valuestring, sizeof(out->sys_location));
    }

    return true;
}

static cJSON *config_to_json(const snmp_config_t *cfg, bool include_runtime_status)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", cfg->enabled);
    cJSON_AddStringToObject(o, "community", cfg->community);
    cJSON_AddStringToObject(o, "sys_name", cfg->sys_name);
    cJSON_AddStringToObject(o, "sys_contact", cfg->sys_contact);
    cJSON_AddStringToObject(o, "sys_location", cfg->sys_location);
    if (include_runtime_status) {
        cJSON_AddBoolToObject(o, "listening", s_listener_started);
    }
    return o;
}

// ---------------------------------------------------------------------
// System-Skalare (Basis-OID .1) - sysDescr/sysUpTime/sysName/sysContact/
// sysLocation, analog zu MIB-II's system-Gruppe.
// ---------------------------------------------------------------------

static s16_t sys_descr_get_value(struct snmp_node_instance *instance, void *value)
{
    static const char descr[] = "TemperaturWatch Sensor-Gateway (ESP32-P4)";
    memcpy(value, descr, sizeof(descr) - 1);
    return sizeof(descr) - 1;
}

static s16_t sys_uptime_get_value(struct snmp_node_instance *instance, void *value)
{
    u32_t ticks = (u32_t)(esp_timer_get_time() / 10000); // TimeTicks = Hundertstelsekunden
    memcpy(value, &ticks, sizeof(ticks));
    return sizeof(ticks);
}

static s16_t sys_name_get_value(struct snmp_node_instance *instance, void *value)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t len = strlen(s_config.sys_name);
    memcpy(value, s_config.sys_name, len);
    xSemaphoreGive(s_lock);
    return (s16_t)len;
}

static s16_t sys_contact_get_value(struct snmp_node_instance *instance, void *value)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t len = strlen(s_config.sys_contact);
    memcpy(value, s_config.sys_contact, len);
    xSemaphoreGive(s_lock);
    return (s16_t)len;
}

static s16_t sys_location_get_value(struct snmp_node_instance *instance, void *value)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t len = strlen(s_config.sys_location);
    memcpy(value, s_config.sys_location, len);
    xSemaphoreGive(s_lock);
    return (s16_t)len;
}

static const struct snmp_scalar_node sys_descr_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(1, SNMP_ASN1_TYPE_OCTET_STRING, sys_descr_get_value);
static const struct snmp_scalar_node sys_uptime_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(2, SNMP_ASN1_TYPE_TIMETICKS, sys_uptime_get_value);
static const struct snmp_scalar_node sys_name_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(3, SNMP_ASN1_TYPE_OCTET_STRING, sys_name_get_value);
static const struct snmp_scalar_node sys_contact_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(4, SNMP_ASN1_TYPE_OCTET_STRING, sys_contact_get_value);
static const struct snmp_scalar_node sys_location_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(5, SNMP_ASN1_TYPE_OCTET_STRING, sys_location_get_value);

static const struct snmp_node *const system_nodes[] = {
    &sys_descr_node.node.node,
    &sys_uptime_node.node.node,
    &sys_name_node.node.node,
    &sys_contact_node.node.node,
    &sys_location_node.node.node,
};
static const struct snmp_tree_node system_group = SNMP_CREATE_TREE_NODE(1, system_nodes);

// ---------------------------------------------------------------------
// Zusaetzlich: die STANDARD-MIB-II-system-Gruppe (1.3.6.1.2.1.1), mit
// derselben Datenquelle wie oben, aber unter der von SNMP-Tools erwarteten
// Standard-OID und -Nummerierung (sysDescr=.1, sysObjectID=.2, sysUpTime=.3,
// sysContact=.4, sysName=.5, sysLocation=.6, sysServices=.7).
//
// Grund: Monitoring-Tools wie CheckMK/Zabbix/PRTG fragen bei der
// Geraete-Erkennung zuerst den STANDARD-sysDescr (1.3.6.1.2.1.1.1.0) ab, um
// ueberhaupt zu erkennen, dass ein Host per SNMP antwortet - unsere eigene
// private MIB unter 1.3.6.1.4.1.99999.1 allein reicht dafuer nicht, auch
// wenn direkte GET-Anfragen darauf einwandfrei funktionieren. Diese Gruppe
// nutzt exakt dieselben Getter-Funktionen wie system_group oben (keine
// Code-Duplikation der eigentlichen Werte), nur unter einer zweiten,
// zusaetzlichen OID registriert. CheckMK fordert zusaetzlich sysObjectID
// (.2) zwingend an (Fehler "Cannot fetch system object OID .1.3.6.1.2.1.1.2.0"
// ohne diese) - daher hier vollstaendig inkl. sysObjectID/sysServices.
static s16_t sys_object_id_get_value(struct snmp_node_instance *instance, void *value)
{
    // Zeigt konventionsgemaess auf die eigene private-MIB-Basis-OID (kein
    // spezifischer registrierter Produkt-Identifier vorhanden, siehe
    // Hinweis zur Platzhalter-PEN oben). Der lwIP-Encoder erwartet hier ein
    // rohes u32_t-Array und die Laenge in BYTES (nicht Elementanzahl).
    static const u32_t oid[] = { 1, 3, 6, 1, 4, 1, 99999, 1 };
    memcpy(value, oid, sizeof(oid));
    return sizeof(oid);
}

static s16_t sys_services_get_value(struct snmp_node_instance *instance, void *value)
{
    // 72 = Layer 4 (End-to-End, 2^3) + Layer 7 (Anwendungen, 2^6), passend
    // fuer ein Geraet, das HTTP/MQTT/SNMP als Endpunkt anbietet (RFC 1213).
    u32_t services = 72;
    memcpy(value, &services, sizeof(services));
    return sizeof(services);
}

static const struct snmp_scalar_node mib2_sys_descr_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(1, SNMP_ASN1_TYPE_OCTET_STRING, sys_descr_get_value);
static const struct snmp_scalar_node mib2_sys_object_id_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(2, SNMP_ASN1_TYPE_OBJECT_ID, sys_object_id_get_value);
static const struct snmp_scalar_node mib2_sys_uptime_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(3, SNMP_ASN1_TYPE_TIMETICKS, sys_uptime_get_value);
static const struct snmp_scalar_node mib2_sys_contact_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(4, SNMP_ASN1_TYPE_OCTET_STRING, sys_contact_get_value);
static const struct snmp_scalar_node mib2_sys_name_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(5, SNMP_ASN1_TYPE_OCTET_STRING, sys_name_get_value);
static const struct snmp_scalar_node mib2_sys_location_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(6, SNMP_ASN1_TYPE_OCTET_STRING, sys_location_get_value);
static const struct snmp_scalar_node mib2_sys_services_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(7, SNMP_ASN1_TYPE_INTEGER, sys_services_get_value);

static const struct snmp_node *const mib2_system_nodes[] = {
    &mib2_sys_descr_node.node.node,
    &mib2_sys_object_id_node.node.node,
    &mib2_sys_uptime_node.node.node,
    &mib2_sys_contact_node.node.node,
    &mib2_sys_name_node.node.node,
    &mib2_sys_location_node.node.node,
    &mib2_sys_services_node.node.node,
};
static const struct snmp_tree_node mib2_system_group = SNMP_CREATE_TREE_NODE(1, mib2_system_nodes);
static const u32_t s_mib2_base_oid[] = { 1, 3, 6, 1, 2, 1, 1 };
static const struct snmp_mib s_mib2_system_mib = SNMP_MIB_CREATE(s_mib2_base_oid, &mib2_system_group.node);

// ---------------------------------------------------------------------
// Sensoren (Basis-OID .2): sensorCount (Skalar) + sensorTable
// Spalten: 1=Index 2=id 3=label 4=type 5=temperature(x10) 6=humidity(x10,
// -1=n/a) 7=valid(1/0) 8=alterSekunden
// ---------------------------------------------------------------------

static bool sensor_item_by_index(u32_t index_1based, cJSON **out_array, cJSON **out_item)
{
    cJSON *arr = NULL;
    sensor_manager_get_readings_json(&arr);
    int count = cJSON_GetArraySize(arr);
    if (index_1based < 1 || (int)index_1based > count) {
        cJSON_Delete(arr);
        return false;
    }
    *out_array = arr;
    *out_item = cJSON_GetArrayItem(arr, (int)index_1based - 1);
    return true;
}

static s16_t sensor_count_get_value(struct snmp_node_instance *instance, void *value)
{
    cJSON *arr = NULL;
    sensor_manager_get_readings_json(&arr);
    u32_t count = (u32_t)cJSON_GetArraySize(arr);
    cJSON_Delete(arr);
    memcpy(value, &count, sizeof(count));
    return sizeof(count);
}

static const struct snmp_scalar_node sensor_count_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(1, SNMP_ASN1_TYPE_INTEGER, sensor_count_get_value);

static char s_sensor_id_buf[40];
static char s_sensor_label_buf[56];
static char s_sensor_type_buf[16];

static snmp_err_t sensor_table_fill_value(const cJSON *item, u32_t column, u32_t index,
                                            union snmp_variant_value *value, u32_t *value_len)
{
    switch (column) {
    case 1:
        value->u32 = index;
        *value_len = sizeof(u32_t);
        return SNMP_ERR_NOERROR;
    case 2: {
        const cJSON *j = cJSON_GetObjectItem(item, "id");
        strlcpy(s_sensor_id_buf, cJSON_IsString(j) ? j->valuestring : "", sizeof(s_sensor_id_buf));
        value->const_ptr = s_sensor_id_buf;
        *value_len = (u32_t)strlen(s_sensor_id_buf);
        return SNMP_ERR_NOERROR;
    }
    case 3: {
        const cJSON *j = cJSON_GetObjectItem(item, "label");
        strlcpy(s_sensor_label_buf, cJSON_IsString(j) ? j->valuestring : "", sizeof(s_sensor_label_buf));
        value->const_ptr = s_sensor_label_buf;
        *value_len = (u32_t)strlen(s_sensor_label_buf);
        return SNMP_ERR_NOERROR;
    }
    case 4: {
        const cJSON *j = cJSON_GetObjectItem(item, "type");
        strlcpy(s_sensor_type_buf, cJSON_IsString(j) ? j->valuestring : "", sizeof(s_sensor_type_buf));
        value->const_ptr = s_sensor_type_buf;
        *value_len = (u32_t)strlen(s_sensor_type_buf);
        return SNMP_ERR_NOERROR;
    }
    case 5: {
        const cJSON *jok = cJSON_GetObjectItem(item, "last_read_ok");
        const cJSON *jt = cJSON_GetObjectItem(item, "temperature_c");
        double v = (cJSON_IsTrue(jok) && cJSON_IsNumber(jt)) ? jt->valuedouble : 0.0;
        value->s32 = (cJSON_IsTrue(jok) && cJSON_IsNumber(jt))
                         ? (s32_t)(v * 10.0 + (v >= 0 ? 0.5 : -0.5)) : -32768;
        *value_len = sizeof(s32_t);
        return SNMP_ERR_NOERROR;
    }
    case 6: {
        const cJSON *jok = cJSON_GetObjectItem(item, "last_read_ok");
        const cJSON *jh = cJSON_GetObjectItem(item, "humidity_pct");
        double v = (cJSON_IsTrue(jok) && cJSON_IsNumber(jh)) ? jh->valuedouble : 0.0;
        value->s32 = (cJSON_IsTrue(jok) && cJSON_IsNumber(jh))
                         ? (s32_t)(v * 10.0 + (v >= 0 ? 0.5 : -0.5)) : -1;
        *value_len = sizeof(s32_t);
        return SNMP_ERR_NOERROR;
    }
    case 7: {
        const cJSON *jok = cJSON_GetObjectItem(item, "last_read_ok");
        value->u32 = cJSON_IsTrue(jok) ? 1 : 0;
        *value_len = sizeof(u32_t);
        return SNMP_ERR_NOERROR;
    }
    case 8: {
        const cJSON *jtime = cJSON_GetObjectItem(item, "last_read_time_ms");
        u32_t age = 0xFFFFFFFF;
        if (cJSON_IsNumber(jtime) && jtime->valuedouble > 0) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            int64_t age_ms = now_ms - (int64_t)jtime->valuedouble;
            age = (age_ms > 0) ? (u32_t)(age_ms / 1000) : 0;
        }
        value->u32 = age;
        *value_len = sizeof(u32_t);
        return SNMP_ERR_NOERROR;
    }
    default:
        return SNMP_ERR_NOSUCHINSTANCE;
    }
}

static snmp_err_t sensor_table_get_cell_value(const u32_t *column, const u32_t *row_oid, u8_t row_oid_len,
                                                union snmp_variant_value *value, u32_t *value_len)
{
    if (row_oid_len != 1) {
        return SNMP_ERR_NOSUCHINSTANCE;
    }
    cJSON *arr = NULL, *item = NULL;
    if (!sensor_item_by_index(row_oid[0], &arr, &item)) {
        return SNMP_ERR_NOSUCHINSTANCE;
    }
    snmp_err_t result = sensor_table_fill_value(item, *column, row_oid[0], value, value_len);
    cJSON_Delete(arr);
    return result;
}

static snmp_err_t sensor_table_get_next_cell_instance_and_value(const u32_t *column, struct snmp_obj_id *row_oid,
                                                                    union snmp_variant_value *value, u32_t *value_len)
{
    u32_t next_index = (row_oid->len >= 1) ? (row_oid->id[0] + 1) : 1;
    cJSON *arr = NULL, *item = NULL;
    if (!sensor_item_by_index(next_index, &arr, &item)) {
        return SNMP_ERR_NOSUCHINSTANCE;
    }
    snmp_err_t result = sensor_table_fill_value(item, *column, next_index, value, value_len);
    cJSON_Delete(arr);
    if (result == SNMP_ERR_NOERROR) {
        u32_t oid_val = next_index;
        snmp_oid_assign(row_oid, &oid_val, 1);
    }
    return result;
}

static const struct snmp_table_simple_col_def sensor_table_columns[] = {
    { 1, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32 },       // sensorIndex
    { 2, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // sensorId
    { 3, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // sensorLabel
    { 4, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // sensorType
    { 5, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_S32 },       // sensorTemperatureX10
    { 6, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_S32 },       // sensorHumidityX10
    { 7, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32 },       // sensorValid
    { 8, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32 },       // sensorAgeSeconds
};

static const struct snmp_table_simple_node sensor_table_node = SNMP_TABLE_CREATE_SIMPLE(
    2, sensor_table_columns, sensor_table_get_cell_value, sensor_table_get_next_cell_instance_and_value);

static const struct snmp_node *const sensors_nodes[] = {
    &sensor_count_node.node.node,
    &sensor_table_node.node.node,
};
static const struct snmp_tree_node sensors_group = SNMP_CREATE_TREE_NODE(2, sensors_nodes);

// ---------------------------------------------------------------------
// IOs (Basis-OID .3): ioCount (Skalar) + ioTable
// Spalten: 1=Index 2=id 3=label 4=type 5=state(1/0)
// ---------------------------------------------------------------------

static bool io_item_by_index(u32_t index_1based, cJSON **out_array, cJSON **out_item)
{
    cJSON *arr = NULL;
    io_manager_get_states_json(&arr);
    int count = cJSON_GetArraySize(arr);
    if (index_1based < 1 || (int)index_1based > count) {
        cJSON_Delete(arr);
        return false;
    }
    *out_array = arr;
    *out_item = cJSON_GetArrayItem(arr, (int)index_1based - 1);
    return true;
}

static s16_t io_count_get_value(struct snmp_node_instance *instance, void *value)
{
    cJSON *arr = NULL;
    io_manager_get_states_json(&arr);
    u32_t count = (u32_t)cJSON_GetArraySize(arr);
    cJSON_Delete(arr);
    memcpy(value, &count, sizeof(count));
    return sizeof(count);
}

static const struct snmp_scalar_node io_count_node =
    SNMP_SCALAR_CREATE_NODE_READONLY(1, SNMP_ASN1_TYPE_INTEGER, io_count_get_value);

static char s_io_id_buf[40];
static char s_io_label_buf[56];
static char s_io_type_buf[16];

static snmp_err_t io_table_fill_value(const cJSON *item, u32_t column, u32_t index,
                                        union snmp_variant_value *value, u32_t *value_len)
{
    switch (column) {
    case 1:
        value->u32 = index;
        *value_len = sizeof(u32_t);
        return SNMP_ERR_NOERROR;
    case 2: {
        const cJSON *j = cJSON_GetObjectItem(item, "id");
        strlcpy(s_io_id_buf, cJSON_IsString(j) ? j->valuestring : "", sizeof(s_io_id_buf));
        value->const_ptr = s_io_id_buf;
        *value_len = (u32_t)strlen(s_io_id_buf);
        return SNMP_ERR_NOERROR;
    }
    case 3: {
        const cJSON *j = cJSON_GetObjectItem(item, "label");
        strlcpy(s_io_label_buf, cJSON_IsString(j) ? j->valuestring : "", sizeof(s_io_label_buf));
        value->const_ptr = s_io_label_buf;
        *value_len = (u32_t)strlen(s_io_label_buf);
        return SNMP_ERR_NOERROR;
    }
    case 4: {
        const cJSON *j = cJSON_GetObjectItem(item, "type");
        strlcpy(s_io_type_buf, cJSON_IsString(j) ? j->valuestring : "", sizeof(s_io_type_buf));
        value->const_ptr = s_io_type_buf;
        *value_len = (u32_t)strlen(s_io_type_buf);
        return SNMP_ERR_NOERROR;
    }
    case 5: {
        const cJSON *j = cJSON_GetObjectItem(item, "state");
        value->u32 = cJSON_IsTrue(j) ? 1 : 0;
        *value_len = sizeof(u32_t);
        return SNMP_ERR_NOERROR;
    }
    default:
        return SNMP_ERR_NOSUCHINSTANCE;
    }
}

static snmp_err_t io_table_get_cell_value(const u32_t *column, const u32_t *row_oid, u8_t row_oid_len,
                                            union snmp_variant_value *value, u32_t *value_len)
{
    if (row_oid_len != 1) {
        return SNMP_ERR_NOSUCHINSTANCE;
    }
    cJSON *arr = NULL, *item = NULL;
    if (!io_item_by_index(row_oid[0], &arr, &item)) {
        return SNMP_ERR_NOSUCHINSTANCE;
    }
    snmp_err_t result = io_table_fill_value(item, *column, row_oid[0], value, value_len);
    cJSON_Delete(arr);
    return result;
}

static snmp_err_t io_table_get_next_cell_instance_and_value(const u32_t *column, struct snmp_obj_id *row_oid,
                                                                union snmp_variant_value *value, u32_t *value_len)
{
    u32_t next_index = (row_oid->len >= 1) ? (row_oid->id[0] + 1) : 1;
    cJSON *arr = NULL, *item = NULL;
    if (!io_item_by_index(next_index, &arr, &item)) {
        return SNMP_ERR_NOSUCHINSTANCE;
    }
    snmp_err_t result = io_table_fill_value(item, *column, next_index, value, value_len);
    cJSON_Delete(arr);
    if (result == SNMP_ERR_NOERROR) {
        u32_t oid_val = next_index;
        snmp_oid_assign(row_oid, &oid_val, 1);
    }
    return result;
}

static const struct snmp_table_simple_col_def io_table_columns[] = {
    { 1, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32 },       // ioIndex
    { 2, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // ioId
    { 3, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // ioLabel
    { 4, SNMP_ASN1_TYPE_OCTET_STRING, SNMP_VARIANT_VALUE_TYPE_CONST_PTR }, // ioType
    { 5, SNMP_ASN1_TYPE_INTEGER,      SNMP_VARIANT_VALUE_TYPE_U32 },       // ioState
};

static const struct snmp_table_simple_node io_table_node = SNMP_TABLE_CREATE_SIMPLE(
    2, io_table_columns, io_table_get_cell_value, io_table_get_next_cell_instance_and_value);

static const struct snmp_node *const io_nodes[] = {
    &io_count_node.node.node,
    &io_table_node.node.node,
};
static const struct snmp_tree_node io_group = SNMP_CREATE_TREE_NODE(3, io_nodes);

// ---------------------------------------------------------------------
// Baum-Wurzel + MIB-Registrierung
// ---------------------------------------------------------------------

static const struct snmp_node *const root_nodes[] = {
    &system_group.node,
    &sensors_group.node,
    &io_group.node,
};
static const struct snmp_tree_node root_group = SNMP_CREATE_TREE_NODE(1, root_nodes);

static const struct snmp_mib s_private_mib = SNMP_MIB_CREATE(s_base_oid, &root_group.node);
static const struct snmp_mib *s_mibs[] = { &s_mib2_system_mib, &s_private_mib };

// ---------------------------------------------------------------------
// Oeffentliche API
// ---------------------------------------------------------------------

esp_err_t snmp_agent_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    cJSON *json = NULL;
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_SNMP, &json);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&s_config, 0, sizeof(s_config));
        strlcpy(s_config.community, "public", sizeof(s_config.community));
        strlcpy(s_config.sys_name, CONFIG_TW_HOSTNAME, sizeof(s_config.sys_name));
    } else if (err == ESP_OK) {
        bool ok = parse_config(json, &s_config);
        cJSON_Delete(json);
        if (!ok) {
            ESP_LOGW(TAG, "Gespeicherte SNMP-Konfiguration ist ungueltig, verwende Defaults");
            memset(&s_config, 0, sizeof(s_config));
            strlcpy(s_config.community, "public", sizeof(s_config.community));
            strlcpy(s_config.sys_name, CONFIG_TW_HOSTNAME, sizeof(s_config.sys_name));
        }
    } else {
        return err;
    }

    if (s_config.enabled) {
        snmp_set_community(s_config.community);
        snmp_set_mibs(s_mibs, LWIP_ARRAYSIZE(s_mibs));
        snmp_init();
        s_listener_started = true;
        ESP_LOGI(TAG, "SNMP-Agent gestartet (UDP/161, Community \"%s\", Basis-OID .1.3.6.1.4.1.99999.1)",
                  s_config.community);
    } else {
        ESP_LOGI(TAG, "SNMP-Agent deaktiviert (in den Einstellungen aktivierbar, erfordert Neustart)");
    }

    return ESP_OK;
}

esp_err_t snmp_agent_get_config_json(cJSON **out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out_config = config_to_json(&s_config, true);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

bool snmp_agent_is_enabled(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled = s_config.enabled;
    xSemaphoreGive(s_lock);
    return enabled;
}

bool snmp_agent_is_listening(void)
{
    return s_listener_started;
}

esp_err_t snmp_agent_set_config_json(const cJSON *config)
{
    snmp_config_t new_cfg;
    if (!parse_config(config, &new_cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *to_store = config_to_json(&new_cfg, false);
    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_SNMP, to_store);
    cJSON_Delete(to_store);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool enabled_changed = (s_config.enabled != new_cfg.enabled);
    s_config = new_cfg;
    if (s_listener_started) {
        snmp_set_community(s_config.community);
    }
    xSemaphoreGive(s_lock);

    if (enabled_changed) {
        ESP_LOGW(TAG, "SNMP aktiviert/deaktiviert geaendert - wird erst nach einem Neustart wirksam");
    }
    return ESP_OK;
}
