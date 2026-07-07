#pragma once
// Thin NVS persistence for the pure KsConfig (P4-007).
#include "ks_config.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Load config from NVS into c (defaults if absent). If no SSID is stored but one
// is set at compile time (CONFIG_KS_WIFI_SSID, a dev convenience), seed it so
// a flashed board joins WiFi without a first-boot SoftAP round-trip.
void ks_config_load(KsConfig* c);

// Persist c to NVS.
esp_err_t ks_config_save(const KsConfig* c);

#ifdef __cplusplus
}
#endif
