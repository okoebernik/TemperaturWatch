#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "nvs.h"

#include "config_store.h"
#include "time_manager.h"

static const char *TAG = "time_manager";

#define DEFAULT_NTP_SERVER "pool.ntp.org"
// Europe/Berlin als POSIX-TZ-String (CET=UTC+1, CEST=UTC+2 mit EU-DST-Regeln:
// Beginn letzter Sonntag im Maerz 02:00, Ende letzter Sonntag im Oktober
// 03:00). Wird als sinnvoller Default angenommen, ist aber ueber das Web-UI
// frei aenderbar.
#define DEFAULT_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

typedef struct {
    bool enabled;
    char ntp_server[64];
    char timezone[64];
} time_config_t;

static time_config_t s_config;
static bool s_sntp_initialized;
static volatile bool s_synced;

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    bool was_synced = s_synced;
    s_synced = true;
    if (!was_synced) {
        ESP_LOGI(TAG, "Zeit erstmalig per NTP synchronisiert");
    }
}

static void apply_timezone_locked(void)
{
    setenv("TZ", s_config.timezone, 1);
    tzset();
}

// Startet/stoppt den SNTP-Client passend zu s_config. Sicher mehrfach
// aufrufbar (deinitialisiert vorher einen ggf. laufenden Client).
static void apply_ntp_locked(void)
{
    if (s_sntp_initialized) {
        esp_netif_sntp_deinit();
        s_sntp_initialized = false;
    }
    if (!s_config.enabled) {
        ESP_LOGI(TAG, "NTP deaktiviert");
        return;
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_config.ntp_server);
    sntp_cfg.start = true;
    sntp_cfg.wait_for_sync = false; // nicht blockieren - Sync laeuft im Hintergrund
    sntp_cfg.sync_cb = on_time_sync;

    esp_err_t err = esp_netif_sntp_init(&sntp_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init fehlgeschlagen: %s", esp_err_to_name(err));
        return;
    }
    s_sntp_initialized = true;
    ESP_LOGI(TAG, "SNTP gestartet, Server: %s", s_config.ntp_server);
}

static cJSON *config_to_json(const time_config_t *cfg, bool include_runtime_status)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", cfg->enabled);
    cJSON_AddStringToObject(o, "ntp_server", cfg->ntp_server);
    cJSON_AddStringToObject(o, "timezone", cfg->timezone);
    if (include_runtime_status) {
        cJSON_AddBoolToObject(o, "synced", s_synced);
    }
    return o;
}

// Fehlende/leere Felder fallen auf sinnvolle Defaults zurueck (wie beim
// SNMP-Community-String) statt den ganzen Request abzulehnen - "enabled"
// ist die Ausnahme und folgt dem Rest der Firmware (fehlt/ungueltig ->
// false), damit ein PUT ohne "enabled"-Feld nicht ueberraschend aktiviert.
static bool parse_config(const cJSON *json, time_config_t *out)
{
    if (!cJSON_IsObject(json)) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    const cJSON *jenabled = cJSON_GetObjectItem(json, "enabled");
    out->enabled = cJSON_IsBool(jenabled) && cJSON_IsTrue(jenabled);

    const cJSON *jserver = cJSON_GetObjectItem(json, "ntp_server");
    if (cJSON_IsString(jserver) && strlen(jserver->valuestring) > 0) {
        strlcpy(out->ntp_server, jserver->valuestring, sizeof(out->ntp_server));
    } else {
        strlcpy(out->ntp_server, DEFAULT_NTP_SERVER, sizeof(out->ntp_server));
    }

    const cJSON *jtz = cJSON_GetObjectItem(json, "timezone");
    if (cJSON_IsString(jtz) && strlen(jtz->valuestring) > 0) {
        strlcpy(out->timezone, jtz->valuestring, sizeof(out->timezone));
    } else {
        strlcpy(out->timezone, DEFAULT_TIMEZONE, sizeof(out->timezone));
    }

    return true;
}

static void apply_config(const time_config_t *new_cfg)
{
    s_config = *new_cfg;
    apply_timezone_locked();
    apply_ntp_locked();
}

esp_err_t time_manager_start(void)
{
    cJSON *json = NULL;
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_TIME, &json);

    time_config_t cfg;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Erststart: NTP standardmaessig aktiv mit pool.ntp.org, ohne dass
        // der Nutzer erst ins Web-UI muss.
        memset(&cfg, 0, sizeof(cfg));
        cfg.enabled = true;
        strlcpy(cfg.ntp_server, DEFAULT_NTP_SERVER, sizeof(cfg.ntp_server));
        strlcpy(cfg.timezone, DEFAULT_TIMEZONE, sizeof(cfg.timezone));
    } else if (err != ESP_OK) {
        return err;
    } else {
        bool ok = parse_config(json, &cfg);
        cJSON_Delete(json);
        if (!ok) {
            ESP_LOGW(TAG, "Gespeicherte Zeit-Konfiguration ist ungueltig, verwende Defaults");
            memset(&cfg, 0, sizeof(cfg));
            cfg.enabled = true;
            strlcpy(cfg.ntp_server, DEFAULT_NTP_SERVER, sizeof(cfg.ntp_server));
            strlcpy(cfg.timezone, DEFAULT_TIMEZONE, sizeof(cfg.timezone));
        }
    }

    apply_config(&cfg);
    return ESP_OK;
}

esp_err_t time_manager_get_config_json(cJSON **out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_config = config_to_json(&s_config, true);
    return ESP_OK;
}

esp_err_t time_manager_set_config_json(const cJSON *config)
{
    time_config_t new_cfg;
    if (!parse_config(config, &new_cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *to_store = config_to_json(&new_cfg, false);
    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_TIME, to_store);
    cJSON_Delete(to_store);
    if (err != ESP_OK) {
        return err;
    }

    apply_config(&new_cfg);
    return ESP_OK;
}

bool time_manager_is_synced(void)
{
    return s_synced;
}

long time_manager_get_utc_offset_s(void)
{
    time_t now = time(NULL);
    struct tm utc_tm;
    gmtime_r(&now, &utc_tm);
    utc_tm.tm_isdst = -1;
    // Klassischer portabler Trick zur Offset-Berechnung ohne Abhaengigkeit
    // von einem tm_gmtoff-Feld (nicht auf allen Libc-Konfigurationen
    // verfuegbar): die UTC-Kalenderfelder werden so interpretiert, als
    // waeren sie lokale Zeit, und ueber mktime() (das die aktuelle TZ
    // beruecksichtigt) zurueck in einen Unix-Zeitstempel gewandelt. Die
    // Differenz zur echten UTC-Zeit ist genau der gesuchte Offset.
    time_t utc_as_local = mktime(&utc_tm);
    return (long)(now - utc_as_local);
}
