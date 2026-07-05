#pragma once
// Thin NVS persistence for the pure P4HubConfig (P4-007).
#include "p4hub_config.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Load config from NVS into c (defaults if absent). If no SSID is stored but one
// is set at compile time (CONFIG_P4HUB_WIFI_SSID, a dev convenience), seed it so
// a flashed board joins WiFi without a first-boot SoftAP round-trip.
void p4hub_config_load(P4HubConfig* c);

// Persist c to NVS.
esp_err_t p4hub_config_save(const P4HubConfig* c);

#ifdef __cplusplus
}
#endif
