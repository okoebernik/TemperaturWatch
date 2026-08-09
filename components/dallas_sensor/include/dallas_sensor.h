#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximale Anzahl Dallas-1-Wire-Geraete (DS18B20-Familie), die pro Bus
// (= pro GPIO) unterstuetzt werden. Der 1-Wire-Standard erlaubt mehr, aber
// das reicht fuer typische Hausinstallationen bei weitem.
#define DALLAS_MAX_DEVICES_PER_BUS 8

typedef struct {
    uint64_t rom_code;
    float temperature_c;
    bool valid;
} dallas_reading_t;

// Sucht alle DS18B20-Geraete am 1-Wire-Bus auf `gpio` und liefert deren
// ROM-Codes (fuer die spaetere Sensor-Zuordnung im Web-UI). *out_count kann
// groesser als max_out sein, dann wurden nur die ersten max_out geliefert.
// Baut und verwirft den Bus bei jedem Aufruf (kein dauerhaftes Handle noetig,
// da Scans nur gelegentlich/manuell ausgeloest werden).
esp_err_t dallas_scan_bus(int gpio, uint64_t *out_rom_codes, size_t max_out, size_t *out_count);

// Triggert eine gemeinsame Temperaturkonvertierung fuer alle Geraete am
// 1-Wire-Bus auf `gpio` und liest anschliessend jedes gefundene Geraet aus.
// *out_count liefert die Anzahl gefuellter Eintraege in `out_readings`
// (begrenzt durch max_out). Ein einzelner Aufruf deckt also einen kompletten
// Bus mit potenziell mehreren Sensoren ab (Multi-Drop).
esp_err_t dallas_read_bus(int gpio, dallas_reading_t *out_readings, size_t max_out, size_t *out_count);

#ifdef __cplusplus
}
#endif
