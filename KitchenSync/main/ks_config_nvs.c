#include "ks_config_nvs.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ks_config_nvs";

#define NS      "kitchensync"
#define KEY     "cfg"
#define KEY_VER "ver"   // P4-014: schema version, its own key (see ks_config.h)

void ks_config_load(KsConfig* c)
{
    // Read into scratch, never straight into *c. nvs_get_blob's size handling is
    // asymmetric: a stored blob LARGER than the buffer errors and copies nothing,
    // but a SMALLER one silently partial-fills and returns ESP_OK. Decoding into
    // the live struct would let a stale short blob half-overwrite it. (P4-014)
    uint8_t buf[sizeof(*c)];
    size_t   sz = 0;
    uint32_t ver = 0;
    bool     have_ver = false;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        have_ver = (nvs_get_u32(h, KEY_VER, &ver) == ESP_OK);
        sz = sizeof(buf);
        if (nvs_get_blob(h, KEY, buf, &sz) != ESP_OK) sz = 0;   // absent/too big -> no blob
        nvs_close(h);
    }

    // Say so out loud: a silent reset to defaults looks identical to "the user's
    // settings vanished", and that's exactly the moment someone needs to know why.
    ks_decode_result r = ks_config_decode(c, buf, sz, have_ver, ver);
    if (r == KS_DECODE_DEFAULTED && sz > 0) {
        ESP_LOGW(TAG, "stored config rejected (ver_present=%d ver=%lu want=%lu, size=%u want=%u)"
                      " -- loaded defaults, re-save to persist",
                 (int)have_ver, (unsigned long)ver, (unsigned long)KS_CONFIG_VERSION,
                 (unsigned)sz, (unsigned)sizeof(*c));
    } else if (r == KS_DECODE_MIGRATED) {
        // Write the upgraded blob back now. Otherwise NVS keeps the old-version
        // bytes and every future boot re-migrates -- and a downgrade to a build
        // that has never heard of v1 would silently reset the user's settings.
        ESP_LOGW(TAG, "migrated config v%lu -> v%lu (wifi creds preserved in slot 0)",
                 (unsigned long)ver, (unsigned long)KS_CONFIG_VERSION);
        esp_err_t e = ks_config_save(c);
        if (e != ESP_OK) ESP_LOGE(TAG, "migration write-back failed: %s", esp_err_to_name(e));
    }

    // Dev convenience: seed slot 0 from the compile-time SSID if NVS has none.
    if (c->wifi[0].ssid[0] == '\0' && sizeof(CONFIG_KS_WIFI_SSID) > 1) {
        strncpy(c->wifi[0].ssid, CONFIG_KS_WIFI_SSID, sizeof(c->wifi[0].ssid) - 1);
        strncpy(c->wifi[0].pass, CONFIG_KS_WIFI_PASSWORD, sizeof(c->wifi[0].pass) - 1);
    }
}

esp_err_t ks_config_save(const KsConfig* c)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    // Blob and version commit together, so a power cut can't leave a version
    // stamp vouching for a blob that was never written (or vice versa).
    e = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (e == ESP_OK) e = nvs_set_u32(h, KEY_VER, KS_CONFIG_VERSION);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
