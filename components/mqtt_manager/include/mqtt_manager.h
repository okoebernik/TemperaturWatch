#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Laedt die MQTT-Konfiguration aus dem Config-Store (deaktiviert, falls noch
// keine existiert) und verbindet bei Bedarf zum Broker. Startet ausserdem
// den periodischen Publish-Task. config_store_init(), sensor_manager_start()
// und io_manager_start() muessen vorher aufgerufen worden sein.
esp_err_t mqtt_manager_start(void);

// Aktuell gespeicherte MQTT-Konfiguration als JSON-Objekt (Aufrufer muss
// cJSON_Delete() aufrufen). Das Passwort-Feld wird aus Sicherheitsgruenden
// immer als leerer String zurueckgegeben.
esp_err_t mqtt_manager_get_config_json(cJSON **out_config);

// Validiert `config`, persistiert es und (re)verbindet den MQTT-Client
// entsprechend. Ein leeres oder fehlendes "password"-Feld behaelt das
// zuvor gespeicherte Passwort bei (siehe mqtt_manager_get_config_json).
// Bei ungueltiger Eingabe wird NICHTS gespeichert und ESP_ERR_INVALID_ARG
// geliefert.
esp_err_t mqtt_manager_set_config_json(const cJSON *config);

// True, wenn aktuell eine Verbindung zum Broker besteht.
bool mqtt_manager_is_connected(void);

// True, wenn MQTT aktuell aktiviert ist (unabhaengig vom Verbindungsstatus).
bool mqtt_manager_is_enabled(void);

#ifdef __cplusplus
}
#endif
