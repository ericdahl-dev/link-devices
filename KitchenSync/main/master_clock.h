#pragma once
// P4-040: internal/tap tempo source + Link/internal arbiter. Pure, host-tested,
// same shape as beat_source.h (ARC-007). KitchenSync (P4) only -- X32Link and
// the LED-only variant stay follow-only; this module is not compiled into them.
//
// The arbiter needs no phase-lock machinery of its own: when solo, it feeds
// ks_tick_step a free-running (xform.valid=false) LinkTimeline sourced from
// this module's micros_per_beat, reusing beat_source_step's existing
// free-run path unchanged.
#include <stdint.h>
#include <stdbool.h>
#include "link_phase.h"        // LinkTimeline
#include "link_measurement.h"  // LinkGhostXForm

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool    has_tempo;        // user tapped/set a BPM, or we auto-seeded one
    int64_t micros_per_beat;  // valid iff has_tempo

    int64_t last_tap_us;
    bool    has_last_tap;

    bool    was_solo;          // edge-detects link_peers>0 -> 0, for seeding
} MasterClock;

void master_clock_reset(MasterClock* mc);

// A tap more than this long after the previous one starts a fresh interval
// instead of computing a tempo from it (stale double-tap guard).
#define MASTER_CLOCK_TAP_TIMEOUT_US 2000000

void master_clock_tap(MasterClock* mc, int64_t now_us);
void master_clock_set_bpm(MasterClock* mc, float bpm);

typedef struct {
    bool           have_session;  // feed straight into KsTickInputs.have_session
    LinkTimeline   tl;
    LinkGhostXForm xform;
    bool           on_internal;   // status/UI: are we currently master-clock-driven?
} MasterArbiterOut;

// Call once per tick, before building KsTickInputs. peers>0 always defers to
// Link (rejoin always wins -- this device never broadcasts, so there is no
// competing session to arbitrate). On the peers>0 -> 0 edge, seeds the
// internal tempo from whatever Link was last showing, if the user hasn't
// already set one -- output is continuous across the transition.
MasterArbiterOut master_clock_arbiter(MasterClock* mc, int link_peers,
                                       bool link_have_session, LinkTimeline link_tl,
                                       LinkGhostXForm link_xform);

#ifdef __cplusplus
}
#endif
