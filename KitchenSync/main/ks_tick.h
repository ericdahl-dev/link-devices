#pragma once
// ARC-015: the clock_out_task decisions as a pure step. clock_out_task was a ~160-line
// god-loop mixing orchestration (beat basis, the reprime fold, the per-output clock
// fan-out, the transport action, the metronome-click gating) with I/O (USB-MIDI,
// speaker, LED) and throttle bookkeeping. The deep modules it drives (beat_source,
// clock_output, transport, metronome) are each host-tested — but the orchestration
// between them was not. This module owns that orchestration and returns a KsTickPlan;
// the loop shrinks to gather-inputs -> step -> execute-plan. Host-tested via test/.
#include <stdint.h>
#include <stdbool.h>
#include "beat_source.h"
#include "clock_ticker.h"
#include "clock_output.h"
#include "metronome.h"
#include "transport.h"
#include "ks_config.h"
#include "link_protocol.h"      // LinkTimeline
#include "link_measurement.h"   // LinkGhostXForm

#ifdef __cplusplus
extern "C" {
#endif

#define KS_TICK_MAX_BURST      96   // re-prime instead of flooding past this backlog
#define KS_TICK_METRO_QUANTUM  4.0  // beats/bar for the bar-1 accent
#define KS_TICK_METRO_BURST    8    // re-prime the click grid past this beat backlog

typedef struct {
    BeatSource  src;
    ClockTicker cts[KS_CLOCK_OUTPUTS];   // one grid per output (P4-010)
    Metronome   mt;
    Transport   tr;
    uint32_t    seen_gen;                // last config-generation acted on (P4-015)
} KsTickState;

// Reset all grids + beat basis; adopt cfg_gen as the current generation.
void ks_tick_reset(KsTickState* st, uint32_t cfg_gen);

typedef struct {
    bool            have_session;    // a committed GhostXForm-backed session timeline
    LinkGhostXForm  xform;
    LinkTimeline    tl;
    int64_t         t_now;           // esp_timer_get_time()
    const KsConfig* cfg;
    uint32_t        cfg_gen;         // g_cfg_gen — a change reprimes the grids
    bool            tp_playing;      // Link transport gate for the metronome
    bool            start_stop_seen; // link_proto_start_stop_seen()
    bool            playing;         // link_proto_playing()
    bool            usb_ready;       // usb_midi_host_ready()
} KsTickInputs;

typedef struct {
    bool   active;   // bs.active — the loop's LED gate + skip
    bool   locked;   // bs.locked — status log
    double beats;    // bs.beats — LED render + status log

    bool   reprime;  // grids repriming happened this tick (basis/session/config edit)

    int             pulses[KS_CLOCK_OUTPUTS];  // 0xF8 count to emit per output (0 = none)
    TransportAction transport;                 // START/STOP/NONE, fan to enabled outputs
    bool            click;                      // emit a metronome click
    bool            click_accent;               // ...as the bar-1 accent
} KsTickPlan;

// Advance one tick: beat_source, the reprime fold, and — when the beat is active — the
// per-output clock fan-out, the transport action, and the metronome-click decision.
// No I/O: the caller emits the plan (USB packets, speaker click, LED). Pure.
KsTickPlan ks_tick_step(KsTickState* st, const KsTickInputs* in);

#ifdef __cplusplus
}
#endif
