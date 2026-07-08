// Pure settled-timeline owner + epoch debounce — see session_timeline.h (ARC-011).
#include "session_timeline.h"
#include "link_phase.h"   // link_phase_timeline_epoch_reset()
#include <string.h>

void session_timeline_reset(SessionTimeline* st) {
    memset(st, 0, sizeof(*st));
}

bool session_timeline_observe(SessionTimeline* st, bool tl_valid,
                              LinkTimeline raw, int64_t now_us) {
    if (!tl_valid) return false;          // no timeline this poll — hold settled

    if (!st->have_settled) {              // first timeline: adopt, no reset
        st->settled      = raw;
        st->have_settled = true;
        return false;
    }

    bool backward = link_phase_timeline_epoch_reset(st->settled.time_origin_us,
                                                    raw.time_origin_us);
    if (backward) {
        // A backward time_origin jump is either a genuine transport re-origin or a
        // joining peer briefly gossiping its own un-synced timeline (P4-028), which
        // last-writer-wins makes indistinguishable here. Debounce: only confirm once
        // the jump PERSISTS past the settle window; a corrective gossip (origin back to
        // the session's) clears the candidate below, so a transient join never resets.
        if (!st->epoch_pending) {
            st->epoch_pending          = true;
            st->epoch_pending_since_us = now_us;
        }
        if (now_us - st->epoch_pending_since_us >= SESSION_TIMELINE_SETTLE_US) {
            st->settled       = raw;      // adopt the confirmed epoch
            st->epoch_pending = false;
            return true;                  // caller resets xform + re-measures
        }
        return false;                     // pending — keep serving the last good timeline
    }

    // Not a backward jump: a normal step, or a correction that undid a transient junk
    // timeline. Clear any pending candidate and track the fresh timeline.
    st->epoch_pending = false;
    st->settled       = raw;
    return false;
}

bool session_timeline_settled(const SessionTimeline* st, LinkTimeline* out) {
    if (!st->have_settled) return false;
    if (out) *out = st->settled;
    return true;
}
