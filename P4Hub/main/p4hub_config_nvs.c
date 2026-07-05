#include "p4hub_config_nvs.h"
#include "sdkconfig.h"
#include "nvs.h"
#include <string.h>

#define NS  "p4hub"
#define KEY "cfg"

void p4hub_config_load(P4HubConfig* c)
{
    p4hub_config_defaults(c);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(*c);
        nvs_get_blob(h, KEY, c, &sz);   // ESP_ERR_NVS_NOT_FOUND leaves defaults
        nvs_close(h);
    }

    // Dev convenience: seed from the compile-time SSID if NVS has none.
    if (c->wifi_ssid[0] == '\0' && sizeof(CONFIG_P4HUB_WIFI_SSID) > 1) {
        strncpy(c->wifi_ssid, CONFIG_P4HUB_WIFI_SSID, sizeof(c->wifi_ssid) - 1);
        strncpy(c->wifi_pass, CONFIG_P4HUB_WIFI_PASSWORD, sizeof(c->wifi_pass) - 1);
    }
}

esp_err_t p4hub_config_save(const P4HubConfig* c)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}
