#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_MANAGER_MAX_SENSORS 32

// Laedt die Sensor-Konfiguration aus dem Config-Store (leere Liste, falls
// noch keine existiert) und startet den Poll-Task. config_store_init() muss
// vorher aufgerufen worden sein.
esp_err_t sensor_manager_start(void);

// Liefert die aktuellen Live-Messwerte aller konfigurierten Sensoren als
// JSON-Array (Aufrufer muss cJSON_Delete() aufrufen). Schema pro Eintrag:
// {id, type, gpio, label, poll_interval_s, rom_id?, has_reading,
//  last_read_ok, temperature_c?, humidity_pct?, last_read_time_ms}
esp_err_t sensor_manager_get_readings_json(cJSON **out_array);

// Liefert die aktuell gespeicherte Sensor-Konfiguration als JSON-Array
// (leeres Array, falls keine Konfiguration existiert). Aufrufer muss
// cJSON_Delete() aufrufen.
esp_err_t sensor_manager_get_config_json(cJSON **out_array);

// Validiert `array` (GPIO muss auf der Stiftleiste liegen, keine Konflikte
// ausser Dallas-Multidrop auf demselben Bus), persistiert es im Config-Store
// und laedt die Laufzeit-Konfiguration neu. Bei ungueltiger Eingabe wird
// NICHTS gespeichert und ESP_ERR_INVALID_ARG geliefert.
esp_err_t sensor_manager_set_config_json(const cJSON *array);

// Sucht einmalig alle DS18B20-Geraete am 1-Wire-Bus auf `gpio` (unabhaengig
// von der gespeicherten Konfiguration - fuer die "Bus scannen"-Funktion im
// Web-UI) und liefert deren ROM-IDs als JSON-Array von Hex-Strings.
esp_err_t sensor_manager_scan_dallas_bus_json(int gpio, cJSON **out_array);

// Liefert den letzten gueltigen Messwert von Sensor `sensor_id` fuer `field`
// ("temperature_c" oder "humidity_pct") - genutzt von io_manager fuer
// Schwellwert-Regeln. *out_valid ist false, wenn der Sensor (noch) keinen
// gueltigen Messwert fuer dieses Feld hat. Liefert ESP_ERR_NOT_FOUND, wenn
// kein Sensor mit `sensor_id` konfiguriert ist.
esp_err_t sensor_manager_get_value(const char *sensor_id, const char *field, float *out_value, bool *out_valid);

#ifdef __cplusplus
}
#endif
