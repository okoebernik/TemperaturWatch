#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Laedt die Zeit-/NTP-Konfiguration aus dem Config-Store (Default beim
// allerersten Start: aktiviert, Server "pool.ntp.org", Zeitzone Europe/Berlin
// als POSIX-TZ-String), setzt die Zeitzone sofort (wirkt global auf
// localtime()/strftime() etc.) und startet bei Bedarf den SNTP-Client.
// esp_netif_init() (siehe net_manager_start()) und config_store_init()
// muessen vorher aufgerufen worden sein. Nicht blockierend - die
// Synchronisation laeuft im Hintergrund, sobald eine Netzwerkverbindung
// besteht.
esp_err_t time_manager_start(void);

// Aktuell gespeicherte Konfiguration als JSON-Objekt
// {enabled, ntp_server, timezone, synced} (Aufrufer muss cJSON_Delete()
// aufrufen). "synced" ist ein reiner Laufzeit-Status und wird beim
// Speichern ignoriert, falls mitgesendet.
esp_err_t time_manager_get_config_json(cJSON **out_config);

// Validiert, persistiert `config` und wendet die Zeitzone sofort an;
// (re)startet den SNTP-Client passend zu "enabled"/"ntp_server" - kein
// Neustart noetig. Liefert ESP_ERR_INVALID_ARG, wenn der Body kein
// JSON-Objekt ist.
esp_err_t time_manager_set_config_json(const cJSON *config);

// true, sobald mindestens einmal eine gueltige Zeit per SNTP empfangen
// wurde. Bleibt danach dauerhaft true (auch wenn NTP spaeter deaktiviert
// wird), da die zuletzt bekannte Zeit per Systemuhr weiterlaeuft.
bool time_manager_is_synced(void);

// Aktueller UTC-Offset der konfigurierten Zeitzone in Sekunden (inkl.
// Sommerzeit, falls gerade aktiv) - fuer Clients, die den lokalen
// Zeitstempel selbst aus "unix_time_s" berechnen wollen, ohne die
// POSIX-TZ-Syntax zu interpretieren (siehe GET /api/system/info).
long time_manager_get_utc_offset_s(void);

#ifdef __cplusplus
}
#endif
