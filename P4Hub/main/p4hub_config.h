#pragma once
// Pure P4Hub runtime config (P4-007): the settings the web UI edits and NVS
// persists. No ESP-IDF dependency — struct + defaults + per-field apply +
// validation, host-tested in test/test_p4hub_config.c. NVS load/save is the thin
// glue in p4hub_config_nvs.c.
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P4HUB_CLOCK_OUTPUTS 4   // Multiclock-style per-output clocks (P4-010)

// Tempo source arbitration (P4-011). Default LINK: follow the Ableton Link
// session (current behavior). MIDI_MASTER: derive tempo from incoming USB-MIDI
// clock and publish it INTO the Link session (the P4 becomes the tempo master).
#define P4HUB_TEMPO_LINK        0   // follow Link (default)
#define P4HUB_TEMPO_MIDI_MASTER 1   // MIDI-in drives Link

// One configurable clock output (routed to a USB-MIDI cable). Division and phase
// are the E-RM-Multiclock-style controls; swing is a deferred follow-up.
typedef struct {
    int enable;        // 0/1 — emit this output
    int cable;         // 0..3 — USB-MIDI virtual cable (Midihub USB A..D)
    int ppqn;          // pulses per beat: 1=1/4, 2=1/8, 4=1/16, 24=MIDI clock, 48=×2 (1..48)
    int phase_mbeats;  // phase nudge, signed milli-beats (±250 = ±1/4 beat) for latency comp
} ClockOutputCfg;

typedef struct {
    char wifi_ssid[33];     // empty => first-boot SoftAP config mode
    char wifi_pass[64];
    int  clock_out_enable;  // 0/1 — master gate for all clock outputs
    int  metronome_enable;  // 0/1 — click the onboard speaker on each beat (P4-006)
    int  metronome_accent;  // 0/1 — accent the bar-1 downbeat (louder/higher)
    ClockOutputCfg clock[P4HUB_CLOCK_OUTPUTS];   // P4-010 per-output clocks
    // NOTE: config persists as a raw NVS blob (p4hub_config_nvs.c). NEW fields MUST
    // be appended at the END so an older, shorter blob still loads its existing
    // fields at the same offsets and new fields keep their p4hub_config_defaults()
    // value (a shorter stored blob leaves trailing bytes untouched). Never insert
    // mid-struct — it shifts every following field on an in-place upgrade.
    int  tempo_source;      // P4HUB_TEMPO_* — Link-follow (default) vs MIDI-in master (P4-011)
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
