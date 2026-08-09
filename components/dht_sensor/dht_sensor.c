#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dht_sensor.h"

static const char *TAG = "dht_sensor";
static portMUX_TYPE s_dht_mux = portMUX_INITIALIZER_UNLOCKED;

// Wartet, bis `gpio` den Pegel `level` erreicht. Liefert die verstrichene
// Zeit in us, oder -1 bei Ueberschreiten von `timeout_us`.
static inline int wait_for_level(int gpio, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) != level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

esp_err_t dht_read(int gpio, dht_type_t type, dht_reading_t *out_reading)
{
    if (out_reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t cfg_err = gpio_config(&io_conf);
    if (cfg_err != ESP_OK) {
        return cfg_err;
    }

    uint8_t data[5] = { 0 };

    // Start-Signal: Leitung >=18ms low ziehen (DHT11-Vorgabe, deckt auch
    // AM2301 ab), dann freigeben und kurz warten.
    gpio_set_level(gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(30);

    // Ab hier ist praezises Mikrosekunden-Timing noetig - Interrupts kurz
    // deaktivieren (ueblich fuer DHT-Bit-Banging, haelt aber fuer bis zu
    // ~5ms den Interrupt-WDT-Spielraum in Anspruch; Reads sind bewusst
    // selten, siehe sensor_manager Poll-Intervalle).
    portENTER_CRITICAL(&s_dht_mux);

    // Antwort des Sensors: ~80us low, ~80us high, dann Start des ersten Bits.
    if (wait_for_level(gpio, 0, 100) < 0 ||
        wait_for_level(gpio, 1, 100) < 0 ||
        wait_for_level(gpio, 0, 100) < 0) {
        portEXIT_CRITICAL(&s_dht_mux);
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < 40; i++) {
        if (wait_for_level(gpio, 1, 80) < 0) {
            portEXIT_CRITICAL(&s_dht_mux);
            return ESP_ERR_TIMEOUT;
        }
        int high_us = wait_for_level(gpio, 0, 100);
        if (high_us < 0) {
            portEXIT_CRITICAL(&s_dht_mux);
            return ESP_ERR_TIMEOUT;
        }
        data[i / 8] <<= 1;
        if (high_us > 40) { // ~70us high = logisch 1, ~26-28us = logisch 0
            data[i / 8] |= 1;
        }
    }

    portEXIT_CRITICAL(&s_dht_mux);

    uint8_t checksum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "GPIO%d: Pruefsumme ungueltig (erwartet 0x%02X, erhalten 0x%02X)",
                  gpio, checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    if (type == DHT_TYPE_DHT11) {
        out_reading->humidity_pct = (float)data[0] + (float)data[1] / 10.0f;
        out_reading->temperature_c = (float)data[2] + (float)data[3] / 10.0f;
    } else {
        uint16_t raw_humidity = ((uint16_t)data[0] << 8) | data[1];
        uint16_t raw_temp = ((uint16_t)(data[2] & 0x7F) << 8) | data[3];
        out_reading->humidity_pct = raw_humidity / 10.0f;
        out_reading->temperature_c = (data[2] & 0x80) ? -(raw_temp / 10.0f) : (raw_temp / 10.0f);
    }

    return ESP_OK;
}
