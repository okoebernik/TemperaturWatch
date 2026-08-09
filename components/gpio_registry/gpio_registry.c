#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "gpio_registry.h"

typedef struct {
    bool used;
    int gpio;
    char owner[24];
} entry_t;

static entry_t s_entries[GPIO_REGISTRY_MAX_ENTRIES];
static SemaphoreHandle_t s_lock;

static SemaphoreHandle_t lock(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    return s_lock;
}

const char *gpio_registry_owner(int gpio)
{
    xSemaphoreTake(lock(), portMAX_DELAY);
    const char *result = NULL;
    for (size_t i = 0; i < GPIO_REGISTRY_MAX_ENTRIES; i++) {
        if (s_entries[i].used && s_entries[i].gpio == gpio) {
            result = s_entries[i].owner;
            break;
        }
    }
    xSemaphoreGive(lock());
    return result;
}

esp_err_t gpio_registry_claim(int gpio, const char *owner)
{
    xSemaphoreTake(lock(), portMAX_DELAY);

    int free_slot = -1;
    for (size_t i = 0; i < GPIO_REGISTRY_MAX_ENTRIES; i++) {
        if (s_entries[i].used && s_entries[i].gpio == gpio) {
            esp_err_t result = (strcmp(s_entries[i].owner, owner) == 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
            xSemaphoreGive(lock());
            return result;
        }
        if (!s_entries[i].used && free_slot < 0) {
            free_slot = (int)i;
        }
    }

    if (free_slot < 0) {
        xSemaphoreGive(lock());
        return ESP_ERR_NO_MEM;
    }

    s_entries[free_slot].used = true;
    s_entries[free_slot].gpio = gpio;
    strlcpy(s_entries[free_slot].owner, owner, sizeof(s_entries[free_slot].owner));

    xSemaphoreGive(lock());
    return ESP_OK;
}

void gpio_registry_release_owner(const char *owner)
{
    xSemaphoreTake(lock(), portMAX_DELAY);
    for (size_t i = 0; i < GPIO_REGISTRY_MAX_ENTRIES; i++) {
        if (s_entries[i].used && strcmp(s_entries[i].owner, owner) == 0) {
            s_entries[i].used = false;
        }
    }
    xSemaphoreGive(lock());
}
