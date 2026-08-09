#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "mdns.h"

#include "config_store.h"
#include "net_manager.h"

#define TW_ETH_MDC_GPIO 31 // aus ETH_ESP32_EMAC_DEFAULT_CONFIG() fuer esp32p4

static const char *TAG = "net_manager";

typedef struct {
    char wifi_ssid[33];
    char wifi_password[64];
    char hostname[32];
} net_config_t;

static net_config_t s_config;
static esp_netif_t *s_eth_netif;
static esp_netif_t *s_wifi_netif;
static bool s_has_ip;
static bool s_eth_connected;
static bool s_wifi_connected;
static bool s_mdns_started;

// Laedt die Netzwerk-Konfiguration aus dem Config-Store, mit den
// Kconfig-Werten (CONFIG_TW_WIFI_SSID etc., siehe Milestone-2-Defaults) als
// Fallback, solange noch nichts ueber das Web-UI gespeichert wurde.
static void load_config(void)
{
    strlcpy(s_config.wifi_ssid, CONFIG_TW_WIFI_SSID, sizeof(s_config.wifi_ssid));
    strlcpy(s_config.wifi_password, CONFIG_TW_WIFI_PASSWORD, sizeof(s_config.wifi_password));
    strlcpy(s_config.hostname, CONFIG_TW_HOSTNAME, sizeof(s_config.hostname));

    cJSON *json = NULL;
    if (config_store_get_json(CONFIG_STORE_KEY_NETWORK, &json) != ESP_OK) {
        return;
    }
    const cJSON *jssid = cJSON_GetObjectItem(json, "wifi_ssid");
    if (cJSON_IsString(jssid)) {
        strlcpy(s_config.wifi_ssid, jssid->valuestring, sizeof(s_config.wifi_ssid));
    }
    const cJSON *jpass = cJSON_GetObjectItem(json, "wifi_password");
    if (cJSON_IsString(jpass) && strlen(jpass->valuestring) > 0) {
        strlcpy(s_config.wifi_password, jpass->valuestring, sizeof(s_config.wifi_password));
    }
    const cJSON *jhost = cJSON_GetObjectItem(json, "hostname");
    if (cJSON_IsString(jhost) && strlen(jhost->valuestring) > 0) {
        strlcpy(s_config.hostname, jhost->valuestring, sizeof(s_config.hostname));
    }
    cJSON_Delete(json);
}

static void start_mdns_once(void)
{
    if (s_mdns_started) {
        return;
    }
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS-Init fehlgeschlagen");
        return;
    }
    mdns_hostname_set(s_config.hostname);
    mdns_instance_name_set("TemperaturWatch Sensor-Gateway");
    s_mdns_started = true;
    ESP_LOGI(TAG, "mDNS bereit unter %s.local", s_config.hostname);
}

static void on_eth_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet: Link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet: Link down");
        s_eth_connected = false;
        s_has_ip = s_eth_connected || s_wifi_connected;
        break;
    default:
        break;
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WLAN getrennt, verbinde erneut ...");
        s_wifi_connected = false;
        s_has_ip = s_eth_connected || s_wifi_connected;
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_ETH_GOT_IP || id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "%s: IP-Adresse " IPSTR,
                  (id == IP_EVENT_ETH_GOT_IP) ? "Ethernet" : "WLAN",
                  IP2STR(&evt->ip_info.ip));
        if (id == IP_EVENT_ETH_GOT_IP) {
            s_eth_connected = true;
        } else {
            s_wifi_connected = true;
        }
        s_has_ip = true;
        start_mdns_once();
    }
}

static esp_err_t eth_start(void)
{
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    // ETH_ESP32_EMAC_DEFAULT_CONFIG() traegt fuer CONFIG_IDF_TARGET_ESP32P4
    // bereits die korrekten RMII-Datenleitungen (TXEN=49, TXD0=34, TXD1=35,
    // CRS_DV=28, RXD0=29, RXD1=30, CLK=50) und MDC/MDIO (31/52) - identisch
    // mit der Waveshare-Referenz (waveshareteam/ESP32-P4-Platform, Beispiel
    // 11_ethernetbasic). Nur PHY-Reset und PHY-Adresse sind board-spezifisch
    // und kommen aus Kconfig.
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    if (mac == NULL) {
        ESP_LOGE(TAG, "esp_eth_mac_new_esp32 fehlgeschlagen");
        return ESP_FAIL;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = CONFIG_TW_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = CONFIG_TW_ETH_PHY_RST_GPIO;
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (phy == NULL) {
        ESP_LOGE(TAG, "esp_eth_phy_new_ip101 fehlgeschlagen");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &eth_handle), TAG,
                          "esp_eth_driver_install fehlgeschlagen");

    esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(eth_handle);
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, glue), TAG,
                          "esp_netif_attach (Ethernet) fehlgeschlagen");

    ESP_RETURN_ON_ERROR(esp_eth_start(eth_handle), TAG, "esp_eth_start fehlgeschlagen");
    ESP_LOGI(TAG, "Ethernet gestartet (MDC=GPIO%d, MDIO=GPIO52, PHY-Reset=GPIO%d, PHY-Adresse=%d)",
              TW_ETH_MDC_GPIO, CONFIG_TW_ETH_PHY_RST_GPIO, CONFIG_TW_ETH_PHY_ADDR);
    return ESP_OK;
}

static esp_err_t wifi_start(void)
{
    s_wifi_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_cfg), TAG, "esp_wifi_init fehlgeschlagen");

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, s_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, s_config.wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = strlen(s_config.wifi_password) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    if (strlen(s_config.wifi_ssid) == 0) {
        ESP_LOGW(TAG, "Keine WLAN-SSID konfiguriert - WLAN-Station bleibt inaktiv "
                       "(im Web-UI unter Netzwerk einrichtbar).");
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG,
                          "esp_wifi_set_config fehlgeschlagen");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start fehlgeschlagen");
    ESP_LOGI(TAG, "WLAN-Station gestartet, verbinde mit SSID \"%s\" (ueber ESP32-C6/esp-hosted)",
              s_config.wifi_ssid);
    return ESP_OK;
}

esp_err_t net_manager_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL));

    load_config();

    esp_err_t eth_err = eth_start();
    if (eth_err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet-Start fehlgeschlagen (%s) - fahre trotzdem mit WLAN fort",
                  esp_err_to_name(eth_err));
    }

    esp_err_t wifi_err = wifi_start();
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "WLAN-Start fehlgeschlagen (%s)", esp_err_to_name(wifi_err));
    }

    return (eth_err == ESP_OK || wifi_err == ESP_OK) ? ESP_OK : ESP_FAIL;
}

bool net_manager_has_ip(void)
{
    return s_has_ip;
}

bool net_manager_wifi_connected(void)
{
    return s_wifi_connected;
}

bool net_manager_eth_connected(void)
{
    return s_eth_connected;
}

esp_err_t net_manager_get_config_json(cJSON **out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "wifi_ssid", s_config.wifi_ssid);
    cJSON_AddStringToObject(o, "wifi_password", ""); // maskiert, siehe net_manager_set_config_json
    cJSON_AddStringToObject(o, "hostname", s_config.hostname);
    *out_config = o;
    return ESP_OK;
}

esp_err_t net_manager_set_config_json(const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    net_config_t new_cfg = s_config; // Passwort standardmaessig beibehalten

    const cJSON *jssid = cJSON_GetObjectItem(config, "wifi_ssid");
    if (!cJSON_IsString(jssid)) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(new_cfg.wifi_ssid, jssid->valuestring, sizeof(new_cfg.wifi_ssid));

    const cJSON *jpass = cJSON_GetObjectItem(config, "wifi_password");
    if (cJSON_IsString(jpass) && strlen(jpass->valuestring) > 0) {
        strlcpy(new_cfg.wifi_password, jpass->valuestring, sizeof(new_cfg.wifi_password));
    }

    const cJSON *jhost = cJSON_GetObjectItem(config, "hostname");
    if (cJSON_IsString(jhost) && strlen(jhost->valuestring) > 0) {
        strlcpy(new_cfg.hostname, jhost->valuestring, sizeof(new_cfg.hostname));
    }

    cJSON *to_store = cJSON_CreateObject();
    cJSON_AddStringToObject(to_store, "wifi_ssid", new_cfg.wifi_ssid);
    cJSON_AddStringToObject(to_store, "wifi_password", new_cfg.wifi_password);
    cJSON_AddStringToObject(to_store, "hostname", new_cfg.hostname);
    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_NETWORK, to_store);
    cJSON_Delete(to_store);
    if (err != ESP_OK) {
        return err;
    }

    s_config = new_cfg;
    ESP_LOGW(TAG, "Netzwerk-Konfiguration geaendert - WLAN-Verbindung und Hostname werden erst "
                   "nach einem Neustart aktualisiert");
    return ESP_OK;
}
