#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Laedt die SNMP-Konfiguration aus dem Config-Store (deaktiviert, falls noch
// keine existiert). Falls aktiviert, wird lwIP's SNMP-Agent (UDP/161)
// gestartet und die eigene MIB (System-Skalare unter .1, Sensoren unter .2,
// IOs unter .3) registriert. config_store_init(), sensor_manager_start()
// und io_manager_start() muessen vorher aufgerufen worden sein.
//
// Hinweis: lwIP bietet keine Funktion, den SNMP-UDP-Listener nach dem Start
// wieder zu stoppen. Das "enabled"-Flag wird daher nur beim Boot ausgewertet;
// ein spaeteres Umschalten via mqtt_agent_set_config_json() aendert
// Community-String/sysName/sysContact/sysLocation sofort, aber ein
// Aktivieren/Deaktivieren des Listeners selbst erfordert einen Neustart
// (wird in der Config-Antwort ueber "restart_required" signalisiert).
esp_err_t snmp_agent_start(void);

// Aktuell gespeicherte SNMP-Konfiguration als JSON-Objekt (Aufrufer muss
// cJSON_Delete() aufrufen).
esp_err_t snmp_agent_get_config_json(cJSON **out_config);

// Validiert `config`, persistiert es und wendet Community-String/sysName/
// sysContact/sysLocation sofort an. Liefert ESP_ERR_INVALID_ARG bei
// ungueltiger Eingabe (nichts wird gespeichert).
esp_err_t snmp_agent_set_config_json(const cJSON *config);

// Leichtgewichtige Laufzeit-Getter (ohne JSON-Allokation) fuer haeufig
// gepollte Statusanzeigen, z.B. die Statuszeile im Web-UI.
bool snmp_agent_is_enabled(void);
bool snmp_agent_is_listening(void);

#ifdef __cplusplus
}
#endif
