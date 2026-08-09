#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "auth_manager.h"
#include "board_pins.h"
#include "config_store.h"
#include "io_manager.h"
#include "mqtt_manager.h"
#include "net_manager.h"
#include "rest_api.h"
#include "sensor_manager.h"
#include "snmp_agent.h"
#include "time_manager.h"

static const char *TAG = "temperaturwatch";

void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "TemperaturWatch startet auf %s (%d Kerne, Rev v%d.%d), %" PRIu32 " MB Flash",
              CONFIG_IDF_TARGET, chip_info.cores,
              chip_info.revision / 100, chip_info.revision % 100,
              flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "%d GPIOs auf der 40-poligen Stiftleiste fuer Sensoren/IOs verfuegbar",
              (int)board_header_gpio_count);

    ESP_ERROR_CHECK(config_store_init());

    esp_err_t auth_err = auth_manager_start();
    if (auth_err != ESP_OK) {
        ESP_LOGE(TAG, "auth_manager_start fehlgeschlagen: %s", esp_err_to_name(auth_err));
    }

    esp_err_t sensor_err = sensor_manager_start();
    if (sensor_err != ESP_OK) {
        ESP_LOGE(TAG, "sensor_manager_start fehlgeschlagen: %s", esp_err_to_name(sensor_err));
    }

    esp_err_t io_err = io_manager_start();
    if (io_err != ESP_OK) {
        ESP_LOGE(TAG, "io_manager_start fehlgeschlagen: %s", esp_err_to_name(io_err));
    }

    esp_err_t net_err = net_manager_start();
    if (net_err != ESP_OK) {
        ESP_LOGE(TAG, "net_manager_start fehlgeschlagen: %s", esp_err_to_name(net_err));
    }

    esp_err_t time_err = time_manager_start();
    if (time_err != ESP_OK) {
        ESP_LOGE(TAG, "time_manager_start fehlgeschlagen: %s", esp_err_to_name(time_err));
    }

    esp_err_t mqtt_err = mqtt_manager_start();
    if (mqtt_err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt_manager_start fehlgeschlagen: %s", esp_err_to_name(mqtt_err));
    }

    esp_err_t snmp_err = snmp_agent_start();
    if (snmp_err != ESP_OK) {
        ESP_LOGE(TAG, "snmp_agent_start fehlgeschlagen: %s", esp_err_to_name(snmp_err));
    }

    esp_err_t rest_err = rest_api_start();
    if (rest_err != ESP_OK) {
        ESP_LOGE(TAG, "rest_api_start fehlgeschlagen: %s", esp_err_to_name(rest_err));
    }

    // Bestaetigt gegenueber dem Bootloader, dass diese Firmware funktioniert
    // (noetig wegen CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) - ohne diesen
    // Aufruf wuerde ein OTA-Update nach dem naechsten Neustart automatisch
    // auf die vorherige Firmware zurückrollen.
    esp_ota_mark_app_valid_cancel_rollback();

    while (1) {
        ESP_LOGI(TAG, "Heartbeat, freier Heap: %" PRIu32 " Bytes, IP vorhanden: %s",
                  esp_get_free_heap_size(), net_manager_has_ip() ? "ja" : "nein");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
