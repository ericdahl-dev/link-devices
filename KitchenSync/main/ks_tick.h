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
#include "transport_launch.h"
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
    // Per-output transport (ESP-011): each output arms and runs independently,
    // exactly as each already owns its own division/phase/swing (P4-010).
    // `tl` decides WHEN (quantized launch), `tr` guarantees exactly one
    // 0xFA/0xFC per transition.
    Transport       tr[KS_CLOCK_OUTPUTS];
    TransportLaunch tl[KS_CLOCK_OUTPUTS];
    BarReset    bar;                     // ESP-015: fires plan.downbeat once per bar
    uint32_t    seen_gen;                // last config-generation acted on (P4-015)
    // P4-039: hold the last known transport-playing state once the session has
    // ever reported it, so a peer-loss reset of in->start_stop_seen (which zeros
    // in->playing too, per link_protocol.c) doesn't snap /status to "not playing"
    // while the clock is still running.
    bool        had_stst;
    bool        held_playing;
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
    // One-shot launch intents from the web UI, consumed this tick (ESP-011).
    TransportLaunchIntent launch[KS_CLOCK_OUTPUTS];
} KsTickInputs;

typedef struct {
    bool   active;   // bs.active — the loop's LED gate + skip
    bool   locked;   // bs.locked — status log
    double beats;    // bs.beats — LED render + status log
    float  bpm;      // P4-039: derived from the settled timeline (in->tl), which
                      // survives peer loss — /status reports this, not a
                      // separately-zeroed peer-count-gated value
    bool   playing;  // P4-039: held transport-playing state (see KsTickState)

    bool   reprime;  // grids repriming happened this tick (basis/session/config edit)

    int             pulses[KS_CLOCK_OUTPUTS];  // 0xF8 count to emit per output (0 = none)
    TransportAction transport[KS_CLOCK_OUTPUTS];  // per-output START/STOP/NONE (ESP-011)
    TransportLaunchState launch_state[KS_CLOCK_OUTPUTS];  // stopped/armed/running, for the UI
    bool            click;                      // emit a metronome click
    bool            click_accent;               // ...as the bar-1 accent
    bool            downbeat;   // ESP-015: crossed into a new bar this tick. The
                                // caller strobes a GPIO so the logic analyzer can
                                // measure the 0xFA-vs-downbeat offset (ESP-011).
                                // Also the reset-pulse source for analog sync (P4-021).
    bool            standby;    // session up, transport stopped: show a heartbeat,
                                // stay silent. Without it, "waiting for play" looks
                                // exactly like a dead board (ESP-009).
} KsTickPlan;

// Advance one tick: beat_source, the reprime fold, and — when the beat is active — the
// per-output clock fan-out, the transport action, and the metronome-click decision.
// No I/O: the caller emits the plan (USB packets, speaker click, LED). Pure.
KsTickPlan ks_tick_step(KsTickState* st, const KsTickInputs* in);

#ifdef __cplusplus
}
#endif
