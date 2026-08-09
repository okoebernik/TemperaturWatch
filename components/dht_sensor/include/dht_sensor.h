#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHT_TYPE_DHT11,
    // AM2301 (= DHT21) und DHT22 verwenden dasselbe Protokoll: 16-Bit-Werte
    // mit einer Nachkommastelle statt der 8-Bit-Ganzzahlwerte des DHT11.
    DHT_TYPE_AM2301,
} dht_type_t;

typedef struct {
    float temperature_c;
    float humidity_pct;
} dht_reading_t;

// Liest einen DHT11/AM2301-Sensor an `gpio` per Bit-Banging aus (kein
// echter 1-Wire-Bus - pro GPIO genau ein Sensor). Blockiert ca. 20-25ms und
// deaktiviert waehrend der eigentlichen Bit-Uebertragung kurzzeitig
// Interrupts fuer praezises Timing (nicht aus einer ISR aufrufen, nicht mit
// hoher Task-Prioritaet nutzen).
//
// Der Sensor benoetigt zwischen zwei Messungen mindestens ~2 Sekunden Pause;
// der Aufrufer (sensor_manager) ist fuer ein passendes Poll-Intervall
// verantwortlich.
esp_err_t dht_read(int gpio, dht_type_t type, dht_reading_t *out_reading);

#ifdef __cplusplus
}
#endif
