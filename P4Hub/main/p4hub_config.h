#pragma once
// Pure P4Hub runtime config (P4-007): the settings the web UI edits and NVS
// persists. No ESP-IDF dependency — struct + defaults + per-field apply +
// validation, host-tested in test/test_p4hub_config.c. NVS load/save is the thin
// glue in p4hub_config_nvs.c.
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];     // empty => first-boot SoftAP config mode
    char wifi_pass[64];
    int  clock_out_enable;  // 0/1 — stream 24-PPQN clock to the USB-MIDI host
    int  midi_cable;        // 0..3 — USB-MIDI virtual cable (Midihub USB A..D)
} P4HubConfig;

void p4hub_config_defaults(P4HubConfig* c);

// True if every field is in range. An empty ssid is valid (it selects SoftAP
// config mode), so callers check ssid separately to decide station vs AP.
bool p4hub_config_valid(const P4HubConfig* c);

// Apply one web form field (key,value). Returns true if the key is known and the
// value passed validation (config updated); false for an unknown key or an
// out-of-range value (config left unchanged). An empty wifi_pass is a no-op
// "keep current" (returns true), so a blank password field never wipes the
// saved one.
bool p4hub_config_set(P4HubConfig* c, const char* key, const char* value);

#ifdef __cplusplus
}
#endif
