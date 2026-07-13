// Pure-ish Link measurement poll — see link_measure_pump.h (ARC-014). No sockets of
// its own; all I/O goes through the LinkMeasureOps vtable, so the poll sequence is
// host-testable with a fake ops.
#include "link_measure_pump.h"
#include "link_measurement.h"
#include "link_protocol.h"

static void run_action(const LinkMeasureOps* ops, const LinkSessionAct* a) {
    switch (a->type) {
        case LS_FLUSH_RX: {
            uint8_t junk[128];
            while (ops->recv_one(junk, sizeof(junk)) > 0) { /* discard stale pongs */ }
            break;
        }
        case LS_START_ATTEMPT: link_measurement_attempt_begin(); break;
        case LS_SEND_PING: {
            uint8_t buf[LINK_MEASUREMENT_PING_MAX_LEN];
            int n = link_measurement_build_ping(buf, sizeof(buf), a->send_time_us,
                                                a->has_prev_ghost, a->prev_ghost_us);
            if (n > 0) ops->send_to(a->ip, a->port, buf, n);
            break;
        }
        case LS_END_OK:      link_measurement_attempt_end(true);  break;
        case LS_END_FAIL:    link_measurement_attempt_end(false); break;
        /* ESP-028: invalidate, do NOT full-reset. The full reset also blanks the P4-038
         * phase-health gauge, and a peer churn fires this -- so routing it through
         * link_measurement_reset() would wipe the lifetime origin-step history on every
         * peer bounce, i.e. exactly when it is worth reading. */
        case LS_RESET_XFORM: link_measurement_invalidate_xform(); break;
    }
}

static void run_all(const LinkMeasureOps* ops, const LinkSessionAct* acts, int n) {
    for (int i = 0; i < n; i++) run_action(ops, &acts[i]);
}

void link_measure_pump(LinkSession* session, const LinkMeasureOps* ops) {
    // ESP-028: sized from the session's own bound, not a literal. on_trigger now emits up to
    // 4 actions (RESET_XFORM + FLUSH_RX + START_ATTEMPT + SEND_PING) and the emitters drop
    // the TAIL when the buffer is short — an under-sized array here would silently swallow
    // the ping, i.e. the attempt would never leave the box.
    LinkSessionAct acts[LINK_SESSION_MAX_ACTIONS];
    const int MAXA = LINK_SESSION_MAX_ACTIONS;
    int n;

    // 1. Epoch reset (ARC-011): settled-timeline debounce confirmed a re-origin.
    if (link_proto_epoch_reset_pending()) {
        n = link_session_on_epoch_reset(session, acts, MAXA);
        run_all(ops, acts, n);
    }

    // 2. Trigger: first peer with an advertised mep4 endpoint.
    uint32_t ip = 0; uint16_t port = 0; bool found = false;
    int peer_count = link_proto_peers();
    for (int i = 0; i < peer_count && !found; i++) {
        if (link_proto_peer_endpoint(i, &ip, &port)) found = true;
    }
    n = link_session_on_trigger(session, found, ip, port, ops->now_us(),
                                link_measurement_active(), acts, MAXA);
    run_all(ops, acts, n);

    if (!link_measurement_active()) return;

    // 3. Drain pongs; the session decides commit-vs-reping per pong.
    uint8_t buf[128];
    int len;
    while ((len = ops->recv_one(buf, sizeof(buf))) > 0) {
        int64_t h_recv = ops->now_us();

        LinkPongFields fields;
        if (!link_measurement_parse_pong(buf, len, &fields)) continue;

        link_measurement_add_pong_samples(h_recv, &fields);
        n = link_session_on_pong(session, h_recv, fields.has_ghost_time, fields.ghost_time_us,
                                 link_measurement_samples_count(), acts, MAXA);
        bool ended = false;
        for (int i = 0; i < n; i++) {
            run_action(ops, &acts[i]);
            if (acts[i].type == LS_END_OK) ended = true;
        }
        if (ended) return;   // attempt committed this poll; nothing left to drive
    }

    // 4. Silence watchdog.
    n = link_session_on_watchdog(session, ops->now_us(), acts, MAXA);
    run_all(ops, acts, n);
}
