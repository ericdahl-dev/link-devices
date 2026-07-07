// Pure Link measurement-session orchestrator — see link_measurement_session.h.
#include "link_measurement_session.h"
#include "link_measurement.h"   // LINK_MEASUREMENT_READY_SAMPLES
#include "link_phase.h"         // link_phase_timeline_epoch_reset()
#include <string.h>

void link_session_reset(LinkSession* s) {
    memset(s, 0, sizeof(*s));
}

int link_session_on_timeline(LinkSession* s, bool tl_valid, int64_t time_origin_us,
                             int64_t now_us, LinkSessionAct* out, int max) {
    if (!tl_valid) return 0;   // no timeline yet — don't touch epoch tracking
    int n = 0;

    bool backward = s->have_last_origin &&
                    link_phase_timeline_epoch_reset(s->last_time_origin_us, time_origin_us);

    if (backward) {
        // A backward time_origin jump is either a genuine transport re-origin (the
        // committed xform now reads garbage against the new ghost epoch — LNK-026
        // bug 1) OR a joining peer briefly gossiping its own un-synced timeline
        // (P4-028), which last-writer-wins in link_protocol.c makes indistinguishable
        // here. Debounce: only act once the jump PERSISTS past the settle window; a
        // corrective gossip (origin back to the session's) clears the candidate in
        // the branch below. A real re-origin persists and confirms; a transient join
        // is corrected within a gossip cycle so it never fires.
        if (!s->epoch_pending) {
            s->epoch_pending          = true;
            s->epoch_pending_since_us = now_us;
        }
        if (now_us - s->epoch_pending_since_us >= LINK_SESSION_EPOCH_SETTLE_US) {
            if (n < max) out[n++].type = LS_RESET_XFORM;
            s->have_ref            = false;
            s->next_measure_us     = 0;
            s->last_time_origin_us = time_origin_us;   // adopt the confirmed epoch
            s->epoch_pending       = false;
        }
        // else: pending — keep last_time_origin_us at the last good value so the jump
        // keeps measuring against it until it confirms or a correction clears it.
        return n;
    }

    // Not a backward jump: a normal forward/small step, or a correction that undid a
    // transient junk timeline. Clear any pending candidate and track normally.
    s->epoch_pending       = false;
    s->last_time_origin_us = time_origin_us;
    s->have_last_origin    = true;
    return n;
}

int link_session_on_trigger(LinkSession* s, bool peer_found, uint32_t ip, uint16_t port,
                            int64_t now_us, bool active, LinkSessionAct* out, int max) {
    if (!peer_found) {
        // No advertised endpoint — the ref vanished or hasn't published mep4 yet.
        // Forget it so a reappearing/new peer re-triggers a fresh attempt.
        s->have_ref = false;
        return 0;
    }

    bool different = !s->have_ref || ip != s->ref_ip || port != s->ref_port;
    bool due       = now_us >= s->next_measure_us;
    if (!((different || due) && !active)) return 0;

    int n = 0;
    // LNK-026 bug 2: flush stale pongs before this attempt's own pings.
    if (n < max) out[n++].type = LS_FLUSH_RX;

    s->ref_ip   = ip;
    s->ref_port = port;
    s->have_ref = true;
    s->have_prev_ghost      = false;
    s->prev_ghost_us        = 0;
    s->consecutive_timeouts = 0;
    s->next_measure_us      = now_us + LINK_SESSION_REMEASURE_US;

    if (n < max) out[n++].type = LS_START_ATTEMPT;

    if (n < max) {
        LinkSessionAct* a = &out[n++];
        a->type = LS_SEND_PING;
        a->ip = ip; a->port = port;
        a->has_prev_ghost = false; a->prev_ghost_us = 0;
        a->send_time_us = now_us;
    }
    s->last_send_us = now_us;
    return n;
}

int link_session_on_pong(LinkSession* s, int64_t h_recv, bool has_ghost, int64_t ghost_us,
                         int samples_count, LinkSessionAct* out, int max) {
    s->consecutive_timeouts = 0;   // a pong resets the silence watchdog

    int n = 0;
    if (samples_count >= LINK_MEASUREMENT_READY_SAMPLES) {
        if (n < max) out[n++].type = LS_END_OK;
        return n;
    }

    // Not enough samples yet — re-ping immediately, carrying this pong's
    // GHostTime as the next ping's PrevGHostTime (round 2+).
    s->have_prev_ghost = has_ghost;
    s->prev_ghost_us   = ghost_us;
    s->last_send_us    = h_recv;

    if (n < max) {
        LinkSessionAct* a = &out[n++];
        a->type = LS_SEND_PING;
        a->ip = s->ref_ip; a->port = s->ref_port;
        a->has_prev_ghost = has_ghost; a->prev_ghost_us = ghost_us;
        a->send_time_us = h_recv;
    }
    return n;
}

int link_session_on_watchdog(LinkSession* s, int64_t now_us, LinkSessionAct* out, int max) {
    if (now_us - s->last_send_us < LINK_SESSION_WATCHDOG_US) return 0;

    int n = 0;
    s->consecutive_timeouts++;
    if (s->consecutive_timeouts >= LINK_SESSION_MAX_TIMEOUTS) {
        if (n < max) out[n++].type = LS_END_FAIL;   // abandon; committed xform untouched
        return n;
    }

    s->last_send_us = now_us;
    if (n < max) {
        LinkSessionAct* a = &out[n++];
        a->type = LS_SEND_PING;
        a->ip = s->ref_ip; a->port = s->ref_port;
        a->has_prev_ghost = s->have_prev_ghost; a->prev_ghost_us = s->prev_ghost_us;
        a->send_time_us = now_us;
    }
    return n;
}
