#pragma once
// KitchenSync Touch runtime config (ESP-016). Trimmed from X32Link's AppConfig:
// NO mixer_ip / model / fx_slot / input_source / fdr -- this is a MIDI product,
// Link-only. The web UI (Inc3) edits it; NVS persists it. Pure validate/set is
// host-tested (test/test_ktouch_config.c); NVS is the thin glue in app_config_nvs.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    int  quantum_beats;      // beats per bar for phase / launch quantize (1..16)
    int  clock_enable;       // 0/1 — emit 24-PPQN MIDI clock on DIN
    int  transport_enable;   // 0/1 — allow PLAY/STOP from the touch screen
    int  play_on_release;    // 0 = toggle on touch (digital DJ), 1 = on release (turntable)
} AppConfig;

void config_defaults(AppConfig* cfg);

// True if every field is in range. An empty ssid is valid (selects AP setup mode),
// so callers check ssid separately to decide station vs AP.
bool config_validate(const AppConfig* cfg);

// ARC-012-style single int setter, shared by the web editor. Tentatively applies
// `value` to `field`, keeps it iff the result validates (returns true); otherwise
// leaves cfg unchanged (returns false).
typedef enum {
    ACF_QUANTUM_BEATS = 1,
    ACF_CLOCK_ENABLE,
    ACF_TRANSPORT_ENABLE,
    ACF_PLAY_ON_RELEASE,
} AppConfigField;

bool app_config_set(AppConfig* cfg, AppConfigField field, int value);

// NVS-backed persistence (app_config_nvs.cpp), namespace "kstouch".
void config_load(AppConfig* cfg);
void config_save(const AppConfig* cfg);

#ifdef __cplusplus
}
#endif
