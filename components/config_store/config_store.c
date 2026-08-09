#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "config_store.h"

static const char *TAG = "config_store";
static const char *NVS_NAMESPACE = "tw_cfg";
static bool s_nvs_ready;

esp_err_t config_store_init(void)
{
    if (s_nvs_ready) {
        return ESP_OK;
    }
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    s_nvs_ready = true;
    return ESP_OK;
}

esp_err_t config_store_get_json(const char *key, cJSON **out_json)
{
    if (key == NULL || out_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_blob(handle, key, NULL, &required_size);
    if (err != ESP_OK || required_size == 0) {
        nvs_close(handle);
        return (err == ESP_OK) ? ESP_ERR_NVS_NOT_FOUND : err;
    }

    char *buf = malloc(required_size + 1);
    if (buf == NULL) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, key, buf, &required_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }
    buf[required_size] = '\0';

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (json == NULL) {
        ESP_LOGE(TAG, "Gespeicherter JSON-Blob fuer \"%s\" ist ungueltig", key);
        return ESP_ERR_INVALID_STATE;
    }

    *out_json = json;
    return ESP_OK;
}

esp_err_t config_store_set_json(const char *key, const cJSON *json)
{
    if (key == NULL || json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *serialized = cJSON_PrintUnformatted(json);
    if (serialized == NULL) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(serialized);
        return err;
    }

    err = nvs_set_blob(handle, key, serialized, strlen(serialized));
    free(serialized);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t config_store_erase(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
}
