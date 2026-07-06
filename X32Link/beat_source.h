#pragma once
// Pure beat-source selector (ARC-007). Owns the policy that used to sit inline in
// P4Hub's clock_out_task: given the current Link session state, decide whether the
// beat position comes from the phase-locked session (a committed GhostXForm ->
// link_phase_beats_now, true downbeat) or the free-running local accumulator
// (beat_clock, correct rate / arbitrary phase), and tell the caller when a basis
// switch or session loss means it must re-prime its tick/click grids so the
// boundary realigns instead of dumping a catch-up burst.
//
// The free-run accumulator is owned here, so this is the single place that knows
// how the beat is sourced. No I/O — the caller passes in the live session state
// (have_session, current xform, timeline, now) each step. Host-tested in
// test/test_beat_source.c. No Arduino/ESP-IDF dependency.
#include <stdint.h>
#include <stdbool.h>
#include "beat_clock.h"
#include "link_phase.h"        // LinkTimeline, link_phase_beats_now
#include "link_measurement.h"  // LinkGhostXForm, link_ghost_xform_host_to_ghost

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    BeatClock bc;          // free-run accumulator (owned)
    bool      was_locked;  // did the last active step read session phase?
    bool      running;     // did the last step have a session?
} BeatSource;

typedef struct {
    bool   active;   // a session is present (a beat exists this step)
    double beats;    // current beat position (valid when active)
    bool   locked;   // true = session phase, false = free-run (valid when active)
    bool   reprime;  // caller must reset its tick/click grids this step
} BeatSourceOut;

void beat_source_reset(BeatSource* s);

// Advance one step. `xform.valid` selects the basis; `now_us` is the monotonic
// local clock. Returns the beat position + whether the caller must re-prime.
BeatSourceOut beat_source_step(BeatSource* s, bool have_session,
                               LinkGhostXForm xform, LinkTimeline tl, int64_t now_us);

#ifdef __cplusplus
}
#endif
