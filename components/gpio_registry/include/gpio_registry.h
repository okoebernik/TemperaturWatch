#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Zentrales, modul-uebergreifendes GPIO-Belegungsregister. sensor_manager
// und io_manager melden hier ihre jeweils genutzten Header-GPIOs an, damit
// beide Module GPIO-Konflikte MITEINANDER erkennen koennen, ohne
// gegenseitig voneinander abhaengen zu muessen (verhindert einen
// zirkulaeren Komponenten-Abhaengigkeitsgraphen).
//
// Konflikte INNERHALB eines Moduls (z.B. mehrere Dallas-Sensoren auf
// demselben 1-Wire-Bus) werden von diesem Register bewusst NICHT
// verhindert: ein Owner kann denselben GPIO beliebig oft fuer sich
// beanspruchen.

#define GPIO_REGISTRY_MAX_ENTRIES 64

// Liefert den Owner-Tag (z.B. "sensor_manager", "io_manager") fuer `gpio`,
// oder NULL, wenn der GPIO aktuell nicht belegt ist.
const char *gpio_registry_owner(int gpio);

// Meldet `gpio` fuer `owner` an. Erfolgreich (ESP_OK), wenn der GPIO frei
// ist oder bereits demselben Owner gehoert. Liefert ESP_ERR_INVALID_STATE,
// wenn der GPIO einem ANDEREN Owner gehoert.
esp_err_t gpio_registry_claim(int gpio, const char *owner);

// Gibt alle aktuell von `owner` gehaltenen GPIOs frei (z.B. vor dem Neuladen
// einer Konfiguration).
void gpio_registry_release_owner(const char *owner);

#ifdef __cplusplus
}
#endif
