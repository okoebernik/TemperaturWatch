#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"

#include "dallas_sensor.h"

static const char *TAG = "dallas_sensor";

// Baut den 1-Wire-Bus an `gpio` auf, sucht alle DS18B20-Geraete und liefert
// sie als offene Handles zurueck (Aufrufer muss jedes Handle mit
// ds18b20_del_device() und den Bus mit onewire_bus_del() wieder freigeben).
static esp_err_t enumerate_ds18b20(int gpio, ds18b20_device_handle_t *out_handles, size_t max_out,
                                     onewire_bus_handle_t *out_bus, size_t *out_count)
{
    *out_count = 0;
    *out_bus = NULL;

    onewire_bus_config_t bus_config = {
        .bus_gpio_num = gpio,
        .flags = { .en_pull_up = 1 },
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10, // 1 Byte ROM-Kommando + 8 Byte ROM-Nummer + 1 Byte Geraete-Kommando
    };
    onewire_bus_handle_t bus = NULL;
    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "1-Wire-Bus an GPIO%d konnte nicht initialisiert werden: %s",
                  gpio, esp_err_to_name(err));
        return err;
    }

    onewire_device_iter_handle_t iter = NULL;
    err = onewire_new_device_iter(bus, &iter);
    if (err != ESP_OK) {
        onewire_bus_del(bus);
        return err;
    }

    size_t count = 0;
    onewire_device_t device;
    esp_err_t search_result;
    do {
        search_result = onewire_device_iter_get_next(iter, &device);
        if (search_result != ESP_OK) {
            break;
        }
        ds18b20_config_t ds_cfg = { 0 };
        ds18b20_device_handle_t handle;
        if (ds18b20_new_device_from_enumeration(&device, &ds_cfg, &handle) != ESP_OK) {
            // Kein DS18B20 (anderer 1-Wire-Geraetetyp) - ignorieren.
            continue;
        }
        if (count < max_out) {
            out_handles[count] = handle;
        } else {
            ds18b20_del_device(handle);
        }
        count++;
    } while (search_result != ESP_ERR_NOT_FOUND);

    onewire_del_device_iter(iter);
    *out_bus = bus;
    *out_count = count;
    return ESP_OK;
}

esp_err_t dallas_scan_bus(int gpio, uint64_t *out_rom_codes, size_t max_out, size_t *out_count)
{
    ds18b20_device_handle_t handles[DALLAS_MAX_DEVICES_PER_BUS];
    onewire_bus_handle_t bus = NULL;
    size_t count = 0;
    esp_err_t err = enumerate_ds18b20(gpio, handles, DALLAS_MAX_DEVICES_PER_BUS, &bus, &count);
    if (err != ESP_OK) {
        return err;
    }

    size_t found = count < DALLAS_MAX_DEVICES_PER_BUS ? count : DALLAS_MAX_DEVICES_PER_BUS;
    size_t reported = found < max_out ? found : max_out;
    for (size_t i = 0; i < reported; i++) {
        onewire_device_address_t addr = 0;
        ds18b20_get_device_address(handles[i], &addr);
        out_rom_codes[i] = (uint64_t)addr;
    }
    if (out_count) {
        *out_count = count;
    }

    for (size_t i = 0; i < found; i++) {
        ds18b20_del_device(handles[i]);
    }
    onewire_bus_del(bus);
    ESP_LOGI(TAG, "Scan GPIO%d: %u DS18B20-Geraet(e) gefunden", gpio, (unsigned)count);
    return ESP_OK;
}

esp_err_t dallas_read_bus(int gpio, dallas_reading_t *out_readings, size_t max_out, size_t *out_count)
{
    ds18b20_device_handle_t handles[DALLAS_MAX_DEVICES_PER_BUS];
    onewire_bus_handle_t bus = NULL;
    size_t count = 0;
    esp_err_t err = enumerate_ds18b20(gpio, handles, DALLAS_MAX_DEVICES_PER_BUS, &bus, &count);
    if (err != ESP_OK) {
        if (out_count) {
            *out_count = 0;
        }
        return err;
    }

    size_t found = count < DALLAS_MAX_DEVICES_PER_BUS ? count : DALLAS_MAX_DEVICES_PER_BUS;
    if (found > 0) {
        ds18b20_trigger_temperature_conversion_for_all(bus);
    }

    size_t reported = found < max_out ? found : max_out;
    for (size_t i = 0; i < reported; i++) {
        onewire_device_address_t addr = 0;
        ds18b20_get_device_address(handles[i], &addr);
        float temperature = 0.0f;
        esp_err_t t_err = ds18b20_get_temperature(handles[i], &temperature);
        out_readings[i].rom_code = (uint64_t)addr;
        out_readings[i].temperature_c = temperature;
        out_readings[i].valid = (t_err == ESP_OK);
        if (t_err != ESP_OK) {
            ESP_LOGW(TAG, "DS18B20 %016llX (GPIO%d): Lesefehler %s",
                      (unsigned long long)addr, gpio, esp_err_to_name(t_err));
        }
    }
    if (out_count) {
        *out_count = reported;
    }

    for (size_t i = 0; i < found; i++) {
        ds18b20_del_device(handles[i]);
    }
    onewire_bus_del(bus);
    return ESP_OK;
}
