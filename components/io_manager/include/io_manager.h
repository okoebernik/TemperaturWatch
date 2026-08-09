#pragma once

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IO_MANAGER_MAX_ENTRIES 32

// Laedt die IO-Konfiguration aus dem Config-Store (leere Liste, falls noch
// keine existiert) und startet den Poll-/Regel-Task. config_store_init()
// und sensor_manager_start() muessen vorher aufgerufen worden sein (fuer
// Schwellwert-Regeln und GPIO-Konfliktpruefung).
esp_err_t io_manager_start(void);

// Aktuelle Zustaende aller konfigurierten IOs als JSON-Array (Aufrufer muss
// cJSON_Delete() aufrufen). Schema pro Eintrag:
// {id, type, gpio, label, invert, state, rule?}
// rule = {sensor_id, field, operator, threshold, hysteresis}. hysteresis
// (>=0, optional beim Speichern, Default 0) verhindert schnelles Ein-/
// Ausschalten nahe am Schwellwert (Schmitt-Trigger-Verhalten): bei "gt"/"gte"
// schaltet der Ausgang erst wieder aus, wenn der Messwert um `hysteresis`
// UNTER den Schwellwert faellt; bei "lt"/"lte" entsprechend erst aus, wenn er
// um `hysteresis` UEBER den Schwellwert steigt.
esp_err_t io_manager_get_states_json(cJSON **out_array);

// Aktuell gespeicherte IO-Konfiguration (leeres Array, falls keine
// existiert). Aufrufer muss cJSON_Delete() aufrufen.
esp_err_t io_manager_get_config_json(cJSON **out_array);

// Validiert `array` (GPIO muss auf der Stiftleiste liegen, darf nicht von
// einem Sensor oder einem anderen IO-Eintrag belegt sein), persistiert es
// und laedt die Laufzeit-Konfiguration neu. Bei ungueltiger Eingabe wird
// NICHTS gespeichert und ESP_ERR_INVALID_ARG geliefert.
esp_err_t io_manager_set_config_json(const cJSON *array);

// Schaltet den Ausgang `id` manuell auf `state`. Liefert
// ESP_ERR_INVALID_STATE, wenn der Ausgang eine aktive Schwellwert-Regel hat
// (die wuerde den manuell gesetzten Zustand beim naechsten Tick ueberschreiben -
// die Regel muss zuerst deaktiviert werden). Liefert ESP_ERR_NOT_FOUND, wenn
// keine passende Ausgangs-ID existiert.
esp_err_t io_manager_set_output(const char *id, bool state);

#ifdef __cplusplus
}
#endif
