#pragma once

#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bekannte Config-Schluessel (NVS-Key innerhalb des "tw_cfg"-Namespace).
// Jeder Schluessel speichert genau ein JSON-Objekt/Array als Blob. Das
// jeweilige Schema wird von den Modulen definiert, die den Schluessel
// nutzen (net_manager, mqtt_manager, snmp_agent, sensor_manager, io_manager).
#define CONFIG_STORE_KEY_NETWORK "network"
#define CONFIG_STORE_KEY_MQTT    "mqtt"
#define CONFIG_STORE_KEY_SNMP    "snmp"
#define CONFIG_STORE_KEY_SENSORS "sensors"
#define CONFIG_STORE_KEY_IO      "io"
#define CONFIG_STORE_KEY_AUTH    "auth"
#define CONFIG_STORE_KEY_TIME    "time"

// Initialisiert NVS, falls noch nicht geschehen (idempotent, kann vor oder
// nach net_manager_start() aufgerufen werden).
esp_err_t config_store_init(void);

// Liest den JSON-Blob unter `key`. Bei Erfolg zeigt *out_json auf ein neu
// alloziertes cJSON-Objekt (Aufrufer muss cJSON_Delete() aufrufen).
// Liefert ESP_ERR_NVS_NOT_FOUND, wenn unter `key` noch keine Config existiert
// (Aufrufer soll dann auf sinnvolle Defaults zurueckfallen).
esp_err_t config_store_get_json(const char *key, cJSON **out_json);

// Serialisiert `json` und speichert es unter `key`. Ueberschreibt einen
// vorhandenen Eintrag.
esp_err_t config_store_set_json(const char *key, const cJSON *json);

// Loescht den Eintrag unter `key` (z.B. fuer Werksreset einzelner Bereiche).
esp_err_t config_store_erase(const char *key);

#ifdef __cplusplus
}
#endif
