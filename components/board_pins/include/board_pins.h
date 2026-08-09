#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Beschreibt einen GPIO, der auf der 40-poligen Stiftleiste (P6) des
// ESP32-P4-WIFI6-POE-ETH herausgefuehrt ist.
typedef struct {
    int gpio;             // GPIO-Nummer am ESP32-P4
    int header_pin;        // Pin-Nummer auf der Stiftleiste (1-40)
    const char *note;      // Hinweis auf eine Standardfunktion, oder NULL
} board_header_gpio_t;

// Alle 28 GPIOs, die auf der Stiftleiste liegen (aus dem Schematic
// ESP32-P4-WIFI6-POE-ETH-Schematic.pdf verifiziert). Alle anderen GPIOs
// des Chips sind intern verdrahtet (Ethernet-PHY, C6-SDIO-Link, SD-Karte,
// USB, MIPI-CSI/DSI, Audio-Codec) und duerfen NICHT fuer Sensoren/IOs
// verwendet werden.
extern const board_header_gpio_t board_header_gpios[];
extern const size_t board_header_gpio_count;

// Prueft, ob ein GPIO auf der Stiftleiste liegt und damit fuer
// Sensor-/IO-Zuweisungen im Web-UI/der REST-API erlaubt ist.
bool board_pins_is_header_gpio(int gpio);

// Liefert den Hinweistext (z.B. "Standard: I2C SDA") fuer einen Header-GPIO,
// oder NULL wenn keiner hinterlegt ist bzw. der GPIO nicht am Header liegt.
const char *board_pins_get_note(int gpio);

#ifdef __cplusplus
}
#endif
