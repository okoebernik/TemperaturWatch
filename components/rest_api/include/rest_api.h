#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Mountet die SPIFFS-"www"-Partition, startet den esp_http_server und
// registriert die REST-Routen (/api/...) sowie den statischen Datei-Handler
// fuer das Web-UI. Muss erst NACH net_manager_start() (fuer sinnvolle
// /api/system/info-Werte) aufgerufen werden, funktioniert aber auch vorher.
esp_err_t rest_api_start(void);

#ifdef __cplusplus
}
#endif
