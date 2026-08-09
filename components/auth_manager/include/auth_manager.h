#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Laedt die Auth-Konfiguration aus dem Config-Store (deaktiviert, falls noch
// keine existiert - Default-Zustand: Web-UI/REST-API frei erreichbar, wie
// bei den meisten lokalen IoT-Gateways). config_store_init() muss vorher
// aufgerufen worden sein.
esp_err_t auth_manager_start(void);

// True, wenn HTTP-Basic-Auth aktuell durchgesetzt wird.
bool auth_manager_is_enabled(void);

// Prueft den Wert eines "Authorization"-Headers (z.B. "Basic
// QWxhZGRpbjpPcGVuU2VzYW1l") gegen die konfigurierten Zugangsdaten.
// Liefert immer true, wenn auth_manager_is_enabled() false ist.
bool auth_manager_check_header(const char *authorization_header);

// Aktuell gespeicherte Konfiguration als JSON-Objekt {enabled, username}
// (Aufrufer muss cJSON_Delete() aufrufen). Das Passwort wird NIE
// zurueckgeliefert (nur als Hash gespeichert).
esp_err_t auth_manager_get_config_json(cJSON **out_config);

// Validiert `config` ({enabled, username, password?}), persistiert es
// (Passwort wird als SHA-256-Hash gespeichert) und wendet es sofort an.
// Ein leeres/fehlendes "password"-Feld behaelt das zuvor gesetzte Passwort
// bei. Liefert ESP_ERR_INVALID_ARG bei ungueltiger Eingabe (z.B. enabled=true
// ohne dass jemals ein Passwort gesetzt wurde).
esp_err_t auth_manager_set_config_json(const cJSON *config);

#ifdef __cplusplus
}
#endif
