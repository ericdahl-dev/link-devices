// Pure Link measurement-session orchestrator — see link_measurement_session.h.
#include "link_measurement_session.h"
#include "link_measurement.h"   // LINK_MEASUREMENT_READY_SAMPLES
#include <string.h>

void link_session_reset(LinkSession* s) {
    memset(s, 0, sizeof(*s));
}

int link_session_on_epoch_reset(LinkSession* s, LinkSessionAct* out, int max) {
    // ARC-011: the settled-timeline owner (session_timeline via link_protocol) has
    // already debounced and confirmed a genuine transport re-origin (LNK-026 bug 1 /
    // P4-028). Drop the committed xform (measured against the old ghost epoch), forget
    // the reference peer, and arm an immediate re-measure against the new epoch.
    int n = 0;
    if (n < max) out[n++].type = LS_RESET_XFORM;
    s->have_ref        = false;
    s->next_measure_us = 0;
    return n;
}

int link_session_on_trigger(LinkSession* s, bool peer_found, uint32_t ip, uint16_t port,
                            int64_t now_us, bool active, LinkSessionAct* out, int max) {
    int n = 0;

    if (!peer_found) {
        // No advertised endpoint — the ref vanished (ByeBye, or PEER_TTL_MS=15 s of
        // silence in link_protocol.c) or hasn't published mep4 yet. Forget it so a
        // reappearing/new peer re-triggers a fresh attempt.
        //
        // ESP-028: forgetting the ref was ALL LNK-031 did here, and that is the defect.
        // The committed GhostXForm was measured against THAT peer's ghost epoch, and
        // link_measurement_have_phase_estimate() reports nothing but s_xform.valid — which
        // nobody was clearing. So the dead session's mapping kept being served as a live
        // phase estimate.
        //
        // Measured on the analyzer (ESP-027): kill the only Link peer, let it rejoin, and
        // `beats` stepped BACKWARDS 276 beats while /status still said sync:1. clock_ticker
        // had primed its grid at beat 290 on the old session; the new session read 14
        // through the old origin; the ticker waited for beats to climb back — 276 beats =
        // 138 SECONDS of zero bytes on the wire, with every counter green.
        //
        // No estimate is honest: beat_source free-runs through it (ESP-027) and
        // master_clock originates. A stale estimate lies, and the wire goes quiet.
        if (s->have_ref) {
            if (n < max) out[n++].type = LS_RESET_XFORM;
            s->have_ref        = false;
            s->next_measure_us = 0;   // measure the instant a peer reappears
        }
        return n;
    }

    // ESP-028: {ip,port} is the only session identity visible from here — link_protocol
    // exposes no session id, and a Link peer that restarts comes back on a NEW ephemeral
    // mep4 port. So a moved endpoint means the committed xform maps to a ghost epoch we
    // can no longer reach: it is a re-target, not a re-measure.
    bool moved     = s->have_ref && (ip != s->ref_ip || port != s->ref_port);
    bool different = !s->have_ref || moved;
    bool due       = now_us >= s->next_measure_us;
    if (!((different || due) && !active)) return 0;

    // ESP-028: drop the dead mapping BEFORE the fresh attempt, never after it commits.
    // Two failure modes ride on this ordering:
    //   1. The attempt needs ~4 round trips and may fail outright — LS_END_FAIL leaves the
    //      committed xform untouched by design (ARC-002). Without this, the stale mapping
    //      is served for the whole attempt, and FOREVER if the attempt fails.
    //   2. link_measurement_attempt_end() slews a commit that lands within 1 s of an
    //      ESTABLISHED origin (LINK_MEASUREMENT_MAX_SLEW_US = 20 ms/commit, P4-038's noise
    //      clamp). Against a DEAD origin that clamp fights the new session instead of
    //      protecting the bar line: a 500 ms epoch difference would need ~25 re-measures
    //      (~50 s at LINK_SESSION_REMEASURE_US) to walk off. Clearing `valid` first makes
    //      the first commit adopt the new session's origin whole — which is exactly what
    //      the clamp's re-origin escape hatch was written to allow.
    //
    // Cost when the endpoint moves but the SESSION does not (peer A leaves a 3-peer session
    // and the pump re-targets to B — link_protocol's swap-remove reshuffles peer[0]): one
    // wasted invalidation. The re-measure lands within milliseconds of the old intercept,
    // far under beat_source's 0.25-beat re-prime threshold, and the gap free-runs. A
    // needless re-measure costs a free-run window in milliseconds. A stale mapping cost 138
    // seconds of silence. The asymmetry decides it.
    if (moved) {
        if (n < max) out[n++].type = LS_RESET_XFORM;
    }

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
