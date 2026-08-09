#pragma once

#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert NVS (falls noch nicht geschehen), Netif/Event-Loop, startet
// Ethernet (IP101 an der internen EMAC/RMII) und WLAN-Station (ueber den
// ESP32-C6-Co-Prozessor via esp_wifi_remote/esp-hosted) und registriert
// mDNS unter dem konfigurierten Hostnamen. Nicht blockierend; der
// Verbindungsstatus wird per ESP_LOG und net_manager_has_ip() sichtbar.
//
// WLAN-SSID/-Passwort und Hostname kommen aus dem Config-Store, mit
// CONFIG_TW_WIFI_SSID/CONFIG_TW_WIFI_PASSWORD/CONFIG_TW_HOSTNAME (Kconfig)
// als Fallback, solange noch nichts ueber das Web-UI gespeichert wurde.
esp_err_t net_manager_start(void);

// Liefert true, sobald mindestens ein Interface (Ethernet oder WLAN) eine
// IP-Adresse erhalten hat.
bool net_manager_has_ip(void);

// Liefert true, solange die WLAN-Station aktuell eine IP-Adresse hat
// (getrennt/nicht konfiguriert -> false).
bool net_manager_wifi_connected(void);

// Liefert true, solange das Ethernet-Interface aktuell eine IP-Adresse hat
// (Kabel gezogen/kein Link -> false).
bool net_manager_eth_connected(void);

// Aktuell gespeicherte Netzwerk-Konfiguration als JSON-Objekt
// {wifi_ssid, wifi_password, hostname, eth_static, wifi_static} (Aufrufer
// muss cJSON_Delete() aufrufen). wifi_password wird aus Sicherheitsgruenden
// immer als leerer String zurueckgeliefert. eth_static/wifi_static sind
// je ein Unterobjekt {enabled, ip, netmask, gateway, dns} - fehlt "dns"
// bzw. ist leer, wird kein expliziter DNS-Server gesetzt (DHCP-Server bzw.
// Firmware-Default bleiben massgeblich). Ist "enabled" false (Default),
// bezieht das jeweilige Interface seine Adresse weiterhin per DHCP.
esp_err_t net_manager_get_config_json(cJSON **out_config);

// Validiert und persistiert `config`. Ein leeres/fehlendes
// "wifi_password"-Feld behaelt das zuvor gesetzte Passwort bei. Bei
// aktivierter statischer IP (eth_static.enabled bzw. wifi_static.enabled)
// muessen ip/netmask/gateway gueltige IPv4-Adressen sein, sonst wird
// ESP_ERR_INVALID_ARG geliefert und NICHTS gespeichert. Wirkt sich erst
// nach einem Neustart aus (kein Live-Rekonfigurieren der laufenden
// WLAN-Verbindung, um Randfaelle im esp-hosted-Stack zu vermeiden).
esp_err_t net_manager_set_config_json(const cJSON *config);

#ifdef __cplusplus
}
#endif
