#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODEL_XR18  1
#define MODEL_X32   2

// ARC-020: schema version for the persisted blob. Bump it whenever AppConfig's
// layout changes, or a stale blob gets read at the wrong offsets. The
// _Static_assert below turns "forgot to bump it" into a compile error.
#define APP_CONFIG_VERSION 1u

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    char mixer_ip[16];
    int  model;         // MODEL_XR18 or MODEL_X32
    int  fx_slot;       // 1–4 (XR18) or 1–8 (X32)
    int  input_source;  // 0 = Ableton Link, 1 = USB MIDI clock
    int  fdr_enable;    // X32FaderDisp: 0 = off, 1 = write dB to scribble names
    int  fdr_chan_count;// X32FaderDisp: 16 (XR18) or 32 (X32)
    int  quantum_beats; // bar-quantized phase: beats per bar, 1-16 (LNK-019)
    int  midi_clock_out_enable; // LNK-027: 0/1 — emit 24-PPQN USB-MIDI clock while following Link
    int  phase_display_mode; // LNK-036: 0 = sweep wheel, 1 = beat-flash dot
    int  dot_beat_color;     // LNK-036: 0xRRGGBB — beats 2..N (parity w/ P4 led_beat_color)
    int  dot_accent_color;   // LNK-036: 0xRRGGBB — bar-1 downbeat (parity w/ P4 led_accent_color)
} AppConfig;

// ARC-020: the size is not the safety mechanism — APP_CONFIG_VERSION is. This
// assert exists to make a layout change impossible to ship silently.
// When it fires: bump APP_CONFIG_VERSION, then update the size here.
//
// This header is compiled as C (host tests, pure modules) AND as C++ (the Arduino
// sketch + the web/touch glue), so the assert has to spell itself both ways: C11
// says _Static_assert, C++ says static_assert, and g++ rejects the C spelling.
#ifdef __cplusplus
#  define APP_CONFIG_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define APP_CONFIG_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif
APP_CONFIG_STATIC_ASSERT(sizeof(AppConfig) == 184,
    "AppConfig layout changed: bump APP_CONFIG_VERSION, then fix this size (ARC-020)");

void config_defaults(AppConfig* cfg);
int  config_model_port(int model);
int  config_model_slot_max(int model);
bool config_validate(const AppConfig* cfg);

// ARC-012: one int-field setter for both editors (web + touch), so per-field ranges
// live only in config_validate. Tentatively applies `value` to `field`, keeps it iff
// the result validates (returns true); otherwise leaves cfg unchanged (returns false).
// model/fx_slot route through config_set_model so the model->slot clamp stays shared.
typedef enum {
    ACF_INPUT_SOURCE = 1, ACF_MODEL, ACF_FX_SLOT, ACF_QUANTUM_BEATS,
    ACF_MIDI_CLOCK_OUT, ACF_FDR_ENABLE, ACF_FDR_CHAN_COUNT,
    ACF_PHASE_DISPLAY_MODE, ACF_DOT_BEAT_COLOR, ACF_DOT_ACCENT_COLOR,
} AppConfigField;

bool app_config_set(AppConfig* cfg, AppConfigField field, int value);

// LNK-032: the model→fx_slot dependency, shared by the web and touch config
// editors so the "slot must be ≤ the model's max" rule lives in one host-tested
// place. Sets model (ignored if not a known model) and clamps fx_slot into
// [1, config_model_slot_max(model)].
void config_set_model(AppConfig* cfg, int model);

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
// one-time migration lives in app_config_nvs.cpp, the only place that can read
// the old keys.
cfg_decode_result config_decode(AppConfig* out, const void* blob, size_t blob_len,
                                bool version_present, uint32_t version);

// NVS-backed persistence (implemented in app_config_nvs.cpp)
void config_load(AppConfig* cfg);
void config_save(const AppConfig* cfg);
void config_clear(void);

#ifdef __cplusplus
}
#endif
