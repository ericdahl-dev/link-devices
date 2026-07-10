#pragma once
// Pure KitchenSync runtime config (P4-007): the settings the web UI edits and NVS
// persists. No ESP-IDF dependency — struct + defaults + per-field apply +
// validation, host-tested in test/test_ks_config.c. NVS load/save is the thin
// glue in ks_config_nvs.c.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define KS_CLOCK_OUTPUTS 4   // Multiclock-style per-output clocks (P4-010)

// P4-014: schema version of the PERSISTED config. Bump on ANY change to
// KsConfig's layout — added, removed, reordered, or resized field. The
// _Static_assert below turns "forgot to bump it" into a compile error.
//
// Deliberately NOT a field inside KsConfig: on the first upgrade from an
// unversioned blob, an in-struct version would read the old blob's first four
// bytes (wifi_ssid[0..3] — arbitrary ASCII) and could alias a plausible
// version. Stored under its own NVS key instead, where "absent" is an
// unambiguous "legacy, don't trust these bytes". See ks_config_nvs.c.
#define KS_CONFIG_VERSION 1u

// One configurable clock output (routed to a USB-MIDI cable). Division, phase and
// swing are the E-RM-Multiclock-style controls.
typedef struct {
    int enable;        // 0/1 — emit this output
    int cable;         // 0..3 — USB-MIDI virtual cable (Midihub USB A..D)
    int ppqn;          // pulses per beat: 1=1/4, 2=1/8, 4=1/16, 24=MIDI clock, 48=×2 (1..48)
    int phase_mbeats;  // phase nudge, signed milli-beats (±250 = ±1/4 beat) for latency comp
    int swing_mbeats;  // swing/shuffle, 0..250 milli-beats of off-eighth delay (0 = straight, P4-013)
} ClockOutputCfg;

typedef struct {
    char wifi_ssid[33];     // empty => first-boot SoftAP config mode
    char wifi_pass[64];
    int  clock_out_enable;  // 0/1 — master gate for all clock outputs
    int  metronome_enable;  // 0/1 — click the onboard speaker on each beat (P4-006)
    int  metronome_accent;  // 0/1 — accent the bar-1 downbeat (louder/higher)
    int  metronome_volume;  // 0..100 — ES8311 codec volume (P4-012)
    int  metronome_voice;   // 0..2 — click voice preset Tone/Click/Wood (P4-012)
    ClockOutputCfg clock[KS_CLOCK_OUTPUTS];   // P4-010 per-output clocks
    int  led_enable;        // 0/1 — visual metronome on the WS2812 strip (P4-018);
                            // independent of metronome_enable
    int  led_brightness;    // 0..100 — master strip brightness % (P4-019)
    int  led_mode;          // 0..2 — pattern: 0 chase, 1 flash, 2 fill
    int  led_fade;          // 0..100 — dim across a beat % (0 steady .. 100 dark by next)
    int  led_beat_color;    // 0xRRGGBB — colour for beats 2..N
    int  led_accent_color;  // 0xRRGGBB — colour for the bar-1 downbeat
    int  follow_beat_enable; // 0/1 -- mic-based tempo detection (P4-020, display only)
} KsConfig;

// P4-014 tripwire. sizeof alone can't detect a same-size field reorder, so it is
// not the safety mechanism — KS_CONFIG_VERSION is. This assert exists to make
// editing the struct impossible without noticing the version constant above.
// When it fires: bump KS_CONFIG_VERSION, then update the size here.
_Static_assert(sizeof(KsConfig) == 228,
               "KsConfig layout changed: bump KS_CONFIG_VERSION, then fix this size (P4-014)");

void ks_config_defaults(KsConfig* c);

// True if every field is in range. An empty ssid is valid (it selects SoftAP
// config mode), so callers check ssid separately to decide station vs AP.
bool ks_config_valid(const KsConfig* c);

// Apply one web form field (key,value). Returns true if the key is known and the
// value passed validation (config updated); false for an unknown key or an
// out-of-range value (config left unchanged). An empty wifi_pass is a no-op
// "keep current" (returns true), so a blank password field never wipes the
// saved one.
bool ks_config_set(KsConfig* c, const char* key, const char* value);

// ARC-016: the one owner of "which fields apply live" (no reboot). Copies exactly the
// live-safe fields from a validated candidate into the running config; the /live
// handler calls this instead of an inline field list. WiFi creds and any reboot-only
// field are deliberately excluded (those go through Save + reboot).
void ks_config_live_safe_copy(KsConfig* dst, const KsConfig* src);

// P4-014: did we load the persisted config, or fall back to defaults?
typedef enum {
    KS_DECODE_OK,          // blob accepted verbatim
    KS_DECODE_DEFAULTED,   // blob absent/legacy/wrong-version/wrong-size/invalid
} ks_decode_result;

// The one owner of "is this persisted blob safe to load?". *out always ends up
// a valid config: either the blob (when it clears every gate) or clean defaults.
//
// The blob is accepted ONLY when all of these hold:
//   version_present  — the NVS "ver" key existed (a legacy blob predates it)
//   version == KS_CONFIG_VERSION
//   blob_len == sizeof(KsConfig)
//   ks_config_valid(blob)
//
// Otherwise *out is defaults. This is what stops a struct-layout change from
// loading bytes shifted into the wrong fields. Pure — ks_config_nvs.c hands over
// whatever NVS held and does no judging of its own. Host-tested.
ks_decode_result ks_config_decode(KsConfig* out, const void* blob, size_t blob_len,
                                  bool version_present, uint32_t version);

#ifdef __cplusplus
}
#endif
