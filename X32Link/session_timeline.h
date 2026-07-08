#pragma once
// ARC-011: the single owner of "what Link timeline is safe to play right now".
//
// The gossiped timeline (link_protocol.c's last-writer-wins store) is observed here.
// A backward time_origin jump is DEBOUNCED (P4-028): a joining peer briefly gossips
// its own un-synced timeline, which looks like a transport re-origin. We hold the
// last-good timeline across a settle window; a corrective gossip clears the candidate,
// a genuine re-origin persists and confirms.
//
// link_protocol owns an instance: link_proto_timeline() returns settled(), so BOTH
// consumers — the epoch/xform logic (via the confirm signal) and the beat/phase math
// (via the returned timeline) — see the same held-last-good timeline with no code
// change. Before ARC-011 the held-good origin was trapped inside LinkSession while the
// beat math read the raw store, so the junk window still glitched the beat. Pure, no
// I/O; see test/test_session_timeline.c.
#include <stdint.h>
#include <stdbool.h>
#include "link_protocol.h"   // LinkTimeline

#ifdef __cplusplus
extern "C" {
#endif

// A backward time_origin jump must persist this long to count as a genuine re-origin
// (not a joining peer's transient un-synced gossip). Tuned to the observed ~500 ms
// peer-join transient; validate/tune on hardware.
#define SESSION_TIMELINE_SETTLE_US 500000   // 500 ms

typedef struct {
    bool         have_settled;          // adopted a first timeline yet?
    LinkTimeline settled;               // last-good timeline, held across a debounce
    bool         epoch_pending;         // a backward jump is being debounced
    int64_t      epoch_pending_since_us;// when the pending jump was first seen
} SessionTimeline;

// Reset all state.
void session_timeline_reset(SessionTimeline* st);

// Observe the raw gossiped timeline. tl_valid=false is a no-op. Returns true iff a
// genuine re-origin just CONFIRMED — the caller must then reset the committed
// GhostXForm and re-measure. Updates the held-last-good `settled`: adopts `raw` on a
// confirmed re-origin, keeps the prior good timeline while a backward jump is pending,
// tracks `raw` on any non-backward step.
bool session_timeline_observe(SessionTimeline* st, bool tl_valid,
                              LinkTimeline raw, int64_t now_us);

// Read the settled (held-last-good) timeline. Returns false until the first observe.
bool session_timeline_settled(const SessionTimeline* st, LinkTimeline* out);

#ifdef __cplusplus
}
#endif
