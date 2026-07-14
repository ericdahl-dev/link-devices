#pragma once
// KitchenSync Touch runtime config (ESP-016). Trimmed from X32Link's AppConfig:
// NO mixer_ip / model / fx_slot / input_source / fdr -- this is a MIDI product,
// Link-only. The web UI (Inc3) edits it; NVS persists it. Pure validate/set is
// host-tested (test/test_ktouch_config.c); NVS is the thin glue in app_config_nvs.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ks_config.h"   // ESP-030: the SHARED WifiCred / KS_WIFI_SLOTS

#ifdef __cplusplus
extern "C" {
#endif

// ARC-020: schema version for the persisted blob. Bump it whenever AppConfig's
// layout changes, or a stale blob gets read at the wrong offsets. The
// _Static_assert below turns "forgot to bump it" into a compile error.
//
// v2 (ESP-030): the single wifi_ssid/wifi_pass became wifi[KS_WIFI_SLOTS], the
// same move the P4 made in ESP-013. A v1 blob is MIGRATED, never discarded --
// see config_decode. One WiFi slot was a deficiency, not a design choice.
// v3 (ESP-030 pt3): the Touch grows RATE + SWING. ktouch_midi_out.cpp said it out
// loud -- "ppqn 24 / swing 0 until the Touch grows RATE + SWING config fields". The
// clock ENGINE already does both (clock_output.c, symlinked in); there were simply
// no config fields to drive it, so a client saw a SWING stepper that could never
// work. A v2 blob is MIGRATED, never discarded.
#define APP_CONFIG_VERSION 3u

// ESP-030: the v1 layout, FROZEN. This is a copy of what SHIPPED, not an alias of
// the current struct: if the live struct changes, this must NOT follow, or the
// migration would read v1 bytes at v2 offsets and hand a device someone else's
// password. Nothing here may ever change.
typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    int  quantum_beats;
    int  clock_enable;
    int  transport_enable;
    int  play_on_release;
    int  nudge_mbeats;
    int  brightness;
} AppConfigV1;

// ESP-030 pt3: the v2 layout, FROZEN. Bytes already in the field — the bench unit
// holds one of these RIGHT NOW. Never edit it; the migration reads v2 bytes at v2
// offsets, and getting that wrong hands a device someone else's password.
typedef struct {
    WifiCred wifi[KS_WIFI_SLOTS];
    int  quantum_beats;
    int  clock_enable;
    int  transport_enable;
    int  play_on_release;
    int  nudge_mbeats;
    int  brightness;
} AppConfigV2;

typedef struct {
    // ESP-030: multiple saved networks, like the P4 (ESP-013). wifi[0].ssid empty
    // => SoftAP config mode, same rule as KsConfig.
    WifiCred wifi[KS_WIFI_SLOTS];
    int  quantum_beats;      // launch/phase quantize in Link beats (1..64). The web
                             // UI edits this in BARS (x4, 4/4); 4 beats = 1 bar.
    int  clock_enable;       // 0/1 — emit 24-PPQN MIDI clock on DIN
    int  transport_enable;   // 0/1 — allow PLAY/STOP from the touch screen
    int  play_on_release;    // 0 = toggle on touch (digital DJ), 1 = on release (turntable)
    int  nudge_mbeats;       // DIN clock phase trim, millibeats (-250..250, tempo-
                             // relative). +ve = clock ahead; slides the RC-505 into
                             // the pocket. Live, no reboot.
    int  brightness;         // LCD backlight, percent (10..100). Live via /bright.

    // ESP-030 pt3. The writer already knew how to do these -- clock_output.c does
    // swing + nudge + ppqn, and it is symlinked in. It was hardcoding `ppqn 24 /
    // swing 0 until the Touch grows RATE + SWING config fields`. Here they are.
    // Same names, same units, same ranges as the P4's ClockOutputCfg, so a client
    // edits ONE field per concept across the whole fleet.
    int  ppqn;               // 1..48 pulses per beat: 24 = MIDI clock, 48 = x2, 12 = /2
    int  swing_mbeats;       // 0..250 millibeats of off-eighth delay (0 = straight)
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
APP_CONFIG_STATIC_ASSERT(sizeof(AppConfig) == 324,
    "AppConfig layout changed: bump APP_CONFIG_VERSION, then fix this size (ARC-020)");

// ESP-030: the FROZEN v1 size. This one must never change — it is the length gate
// the migration reads old blobs through. If this assert ever fires, someone has
// edited a layout that already shipped, and every field device's config is at risk.
APP_CONFIG_STATIC_ASSERT(sizeof(AppConfigV1) == 152,
    "the v1 layout is FROZEN — it describes bytes already in the field (ESP-030)");
APP_CONFIG_STATIC_ASSERT(sizeof(AppConfigV2) == 316,
    "the v2 layout is FROZEN — the bench unit holds one of these (ESP-030 pt3)");

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
    ACF_PPQN,           // ESP-030 pt3
    ACF_SWING_MBEATS,   // ESP-030 pt3
} AppConfigField;

bool app_config_set(AppConfig* cfg, AppConfigField field, int value);

// ARC-020: did we load the persisted config, or fall back to defaults?
typedef enum {
    CFG_DECODE_OK,          // blob accepted verbatim
    CFG_DECODE_DEFAULTED,   // blob absent/unversioned/wrong-version/wrong-size/invalid
    // ESP-030: an OLD but READABLE blob, upgraded rather than thrown away.
    //
    // This case exists because the alternative is unacceptable: defaulting on a
    // version bump makes every Touch in the field lose its WiFi credentials on the
    // next boot, fall back to the KitchenSync-Setup SoftAP, and drop off the
    // network. The P4 learned this in ESP-013 ("a version bump that silently
    // forgot [wifi creds]"). The caller should WRITE THE UPGRADED BLOB BACK, or
    // every future boot re-migrates.
    CFG_DECODE_MIGRATED,
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
