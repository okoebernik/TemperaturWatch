#include <string.h>
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include "config_store.h"
#include "auth_manager.h"

static const char *TAG = "auth_manager";

typedef struct {
    bool enabled;
    char username[32];
    uint8_t password_hash[32];
    bool has_password;
} auth_config_t;

static auth_config_t s_config;

static void hash_password(const char *password, uint8_t out_hash[32])
{
    mbedtls_sha256((const unsigned char *)password, strlen(password), out_hash, 0);
}

static bool consttime_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void hash_to_hex(const uint8_t hash[32], char out_hex[65])
{
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out_hex[i * 2] = hex[hash[i] >> 4];
        out_hex[i * 2 + 1] = hex[hash[i] & 0x0F];
    }
    out_hex[64] = '\0';
}

static bool hex_to_hash(const char *hex, uint8_t out_hash[32])
{
    if (strlen(hex) != 64) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        int hi = hex_digit_value(hex[i * 2]);
        int lo = hex_digit_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out_hash[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static cJSON *config_to_json_storage(const auth_config_t *cfg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", cfg->enabled);
    cJSON_AddStringToObject(o, "username", cfg->username);
    cJSON_AddBoolToObject(o, "has_password", cfg->has_password);
    if (cfg->has_password) {
        char hex[65];
        hash_to_hex(cfg->password_hash, hex);
        cJSON_AddStringToObject(o, "password_hash", hex);
    }
    return o;
}

static bool parse_config_storage(const cJSON *json, auth_config_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsObject(json)) {
        return false;
    }
    const cJSON *jenabled = cJSON_GetObjectItem(json, "enabled");
    out->enabled = cJSON_IsBool(jenabled) && cJSON_IsTrue(jenabled);

    const cJSON *juser = cJSON_GetObjectItem(json, "username");
    strlcpy(out->username,
             (cJSON_IsString(juser) && strlen(juser->valuestring) > 0) ? juser->valuestring : "admin",
             sizeof(out->username));

    const cJSON *jhash = cJSON_GetObjectItem(json, "password_hash");
    if (cJSON_IsString(jhash) && hex_to_hash(jhash->valuestring, out->password_hash)) {
        out->has_password = true;
    }
    return true;
}

esp_err_t auth_manager_start(void)
{
    cJSON *json = NULL;
    esp_err_t err = config_store_get_json(CONFIG_STORE_KEY_AUTH, &json);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&s_config, 0, sizeof(s_config));
        strlcpy(s_config.username, "admin", sizeof(s_config.username));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    bool ok = parse_config_storage(json, &s_config);
    cJSON_Delete(json);
    if (!ok) {
        ESP_LOGW(TAG, "Gespeicherte Auth-Konfiguration ist ungueltig, Login bleibt deaktiviert");
        memset(&s_config, 0, sizeof(s_config));
        strlcpy(s_config.username, "admin", sizeof(s_config.username));
    }
    if (s_config.enabled) {
        ESP_LOGI(TAG, "HTTP-Basic-Auth aktiv (Benutzer \"%s\")", s_config.username);
    }
    return ESP_OK;
}

bool auth_manager_is_enabled(void)
{
    return s_config.enabled;
}

bool auth_manager_check_header(const char *authorization_header)
{
    if (!s_config.enabled) {
        return true;
    }
    if (authorization_header == NULL || strncmp(authorization_header, "Basic ", 6) != 0) {
        return false;
    }

    const char *b64 = authorization_header + 6;
    unsigned char decoded[96];
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                                (const unsigned char *)b64, strlen(b64)) != 0) {
        return false;
    }
    decoded[decoded_len] = '\0';

    char *colon = strchr((char *)decoded, ':');
    if (colon == NULL) {
        return false;
    }
    *colon = '\0';
    const char *user = (const char *)decoded;
    const char *pass = colon + 1;

    if (strcmp(user, s_config.username) != 0 || !s_config.has_password) {
        return false;
    }

    uint8_t pass_hash[32];
    hash_password(pass, pass_hash);
    return consttime_equal(pass_hash, s_config.password_hash, sizeof(pass_hash));
}

esp_err_t auth_manager_get_config_json(cJSON **out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", s_config.enabled);
    cJSON_AddStringToObject(o, "username", s_config.username);
    cJSON_AddBoolToObject(o, "password_set", s_config.has_password);
    *out_config = o;
    return ESP_OK;
}

esp_err_t auth_manager_set_config_json(const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    auth_config_t new_cfg = s_config; // Passwort-Hash standardmaessig beibehalten

    const cJSON *jenabled = cJSON_GetObjectItem(config, "enabled");
    new_cfg.enabled = cJSON_IsBool(jenabled) && cJSON_IsTrue(jenabled);

    const cJSON *juser = cJSON_GetObjectItem(config, "username");
    if (cJSON_IsString(juser) && strlen(juser->valuestring) > 0) {
        strlcpy(new_cfg.username, juser->valuestring, sizeof(new_cfg.username));
    } else if (strlen(new_cfg.username) == 0) {
        strlcpy(new_cfg.username, "admin", sizeof(new_cfg.username));
    }

    const cJSON *jpass = cJSON_GetObjectItem(config, "password");
    if (cJSON_IsString(jpass) && strlen(jpass->valuestring) > 0) {
        hash_password(jpass->valuestring, new_cfg.password_hash);
        new_cfg.has_password = true;
    }

    if (new_cfg.enabled && !new_cfg.has_password) {
        return ESP_ERR_INVALID_ARG; // Login kann nicht ohne Passwort aktiviert werden
    }

    cJSON *to_store = config_to_json_storage(&new_cfg);
    esp_err_t err = config_store_set_json(CONFIG_STORE_KEY_AUTH, to_store);
    cJSON_Delete(to_store);
    if (err != ESP_OK) {
        return err;
    }

    s_config = new_cfg;
    return ESP_OK;
}
