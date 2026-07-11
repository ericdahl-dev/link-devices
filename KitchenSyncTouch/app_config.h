#pragma once
// KitchenSync Touch runtime config (ESP-016). Trimmed from X32Link's AppConfig:
// NO mixer_ip / model / fx_slot / input_source / fdr -- this is a MIDI product,
// Link-only. The web UI (Inc3) edits it; NVS persists it. Pure validate/set is
// host-tested (test/test_ktouch_config.c); NVS is the thin glue in app_config_nvs.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ARC-020: schema version for the persisted blob. Bump it whenever AppConfig's
// layout changes, or a stale blob gets read at the wrong offsets. The
// _Static_assert below turns "forgot to bump it" into a compile error.
#define APP_CONFIG_VERSION 1u

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    int  quantum_beats;      // launch/phase quantize in Link beats (1..64). The web
                             // UI edits this in BARS (x4, 4/4); 4 beats = 1 bar.
    int  clock_enable;       // 0/1 — emit 24-PPQN MIDI clock on DIN
    int  transport_enable;   // 0/1 — allow PLAY/STOP from the touch screen
    int  play_on_release;    // 0 = toggle on touch (digital DJ), 1 = on release (turntable)
    int  nudge_mbeats;       // DIN clock phase trim, millibeats (-250..250, tempo-
                             // relative). +ve = clock ahead; slides the RC-505 into
                             // the pocket. Live via /nudge, no reboot.
    int  brightness;         // LCD backlight, percent (10..100). Live via /bright.
} AppConfig;

// ARC-020: the size is not the safety mechanism — APP_CONFIG_VERSION is. This
// assert exists to make a layout change impossible to ship silently.
// When it fires: bump APP_CONFIG_VERSION, then update the size here.
//
// This header is compiled as C (host tests, pure modules) AND as C++ (the Arduino
// sketch + the web glue), so the assert has to spell itself both ways: C11 says
// _Static_assert, C++ says static_assert, and g++ rejects the C spelling.
#ifdef __cplusplus
#  define APP_CONFIG_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define APP_CONFIG_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
APP_CONFIG_STATIC_ASSERT(sizeof(AppConfig) == 152,
    "AppConfig layout changed: bump APP_CONFIG_VERSION, then fix this size (ARC-020)");

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
    ACF_NUDGE_MBEATS,
    ACF_BRIGHTNESS,
} AppConfigField;

bool app_config_set(AppConfig* cfg, AppConfigField field, int value);

// ARC-020: did we load the persisted config, or fall back to defaults?
typedef enum {
    CFG_DECODE_OK,          // blob accepted verbatim
    CFG_DECODE_DEFAULTED,   // blob absent/unversioned/wrong-version/wrong-size/invalid
} cfg_decode_result;

// The one owner of "is this persisted blob safe to load?". *out always ends up a
// valid config: either the blob (when it clears every gate) or clean defaults.
//
// Accepted verbatim ONLY when all of these hold:
//   version_present  — the NVS "ver" key existed
//   version == APP_CONFIG_VERSION
//   blob_len == sizeof(AppConfig)
//   config_validate(blob)
//
// Otherwise *out is defaults. This is what stops a struct-layout change from
// loading bytes shifted into the wrong fields. Pure — app_config_nvs.cpp hands
// over whatever NVS held and does no judging of its own. Host-tested.
//
// NOTE: the pre-ARC-020 format was field-keyed Preferences, not a blob, so it
// cannot be decoded here (it never reaches this function as bytes). That
// one-time migration lives in app_config_nvs.cpp, which is the only place that
// can read the old keys.
cfg_decode_result config_decode(AppConfig* out, const void* blob, size_t blob_len,
                                bool version_present, uint32_t version);

// NVS-backed persistence (app_config_nvs.cpp), namespace "kstouch".
void config_load(AppConfig* cfg);
void config_save(const AppConfig* cfg);

#ifdef __cplusplus
}
#endif
