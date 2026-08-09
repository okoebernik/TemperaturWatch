#include "board_pins.h"

const board_header_gpio_t board_header_gpios[] = {
    { .gpio = 0,  .header_pin = 24, .note = NULL },
    { .gpio = 1,  .header_pin = 21, .note = NULL },
    { .gpio = 2,  .header_pin = 22, .note = NULL },
    { .gpio = 3,  .header_pin = 20, .note = NULL },
    { .gpio = 4,  .header_pin = 17, .note = NULL },
    { .gpio = 5,  .header_pin = 15, .note = NULL },
    { .gpio = 6,  .header_pin = 16, .note = NULL },
    { .gpio = 7,  .header_pin = 4,  .note = "Standard: I2C SDA" },
    { .gpio = 8,  .header_pin = 6,  .note = "Standard: I2C SCL" },
    { .gpio = 20, .header_pin = 14, .note = NULL },
    { .gpio = 21, .header_pin = 12, .note = NULL },
    { .gpio = 22, .header_pin = 11, .note = NULL },
    { .gpio = 23, .header_pin = 8,  .note = NULL },
    { .gpio = 24, .header_pin = 28, .note = NULL },
    { .gpio = 25, .header_pin = 27, .note = NULL },
    { .gpio = 26, .header_pin = 32, .note = NULL },
    { .gpio = 27, .header_pin = 37, .note = NULL },
    { .gpio = 32, .header_pin = 25, .note = NULL },
    { .gpio = 33, .header_pin = 30, .note = NULL },
    { .gpio = 36, .header_pin = 23, .note = NULL },
    { .gpio = 37, .header_pin = 7,  .note = "Standard: Debug-UART TX" },
    { .gpio = 38, .header_pin = 9,  .note = "Standard: Debug-UART RX" },
    { .gpio = 45, .header_pin = 39, .note = NULL },
    { .gpio = 46, .header_pin = 35, .note = NULL },
    { .gpio = 47, .header_pin = 38, .note = NULL },
    { .gpio = 48, .header_pin = 34, .note = NULL },
    { .gpio = 53, .header_pin = 36, .note = NULL },
    { .gpio = 54, .header_pin = 31, .note = NULL },
};

const size_t board_header_gpio_count = sizeof(board_header_gpios) / sizeof(board_header_gpios[0]);

bool board_pins_is_header_gpio(int gpio)
{
    for (size_t i = 0; i < board_header_gpio_count; i++) {
        if (board_header_gpios[i].gpio == gpio) {
            return true;
        }
    }
    return false;
}

const char *board_pins_get_note(int gpio)
{
    for (size_t i = 0; i < board_header_gpio_count; i++) {
        if (board_header_gpios[i].gpio == gpio) {
            return board_header_gpios[i].note;
        }
    }
    return NULL;
}
