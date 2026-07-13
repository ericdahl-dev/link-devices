// Host tests for the pure Link measurement-session orchestrator (LNK-031).
// These encode the LNK-026 regressions as unit tests: they used to be catchable
// only by flashing hardware.
#include "unity.h"
#include "link_measurement_session.h"
#include "link_measurement.h"   // LINK_MEASUREMENT_READY_SAMPLES

static LinkSession s;
static LinkSessionAct acts[8];
void setUp(void)    { link_session_reset(&s); link_measurement_reset(); }
void tearDown(void) {}

// Convenience: start an attempt against a peer and return the action count.
static int start(uint32_t ip, uint16_t port, int64_t now) {
    return link_session_on_trigger(&s, true, ip, port, now, /*active=*/false, acts, 8);
}

/* ESP-028: the stale-xform tests below need the REAL committed-mapping state, not a mock —
 * the whole defect was that link_measurement_have_phase_estimate() kept saying `true`. So
 * this file links link_measurement.c and executes the emitted actions against it, the same
 * way link_measure_pump.c does on the device (minus the sockets, which no test needs to
 * assert on here). If we only checked that the right ENUM came out, we would be testing the
 * shape of the fix instead of the property that was violated. */
static void run_acts(int n) {
    for (int i = 0; i < n; i++) {
        switch (acts[i].type) {
            case LS_FLUSH_RX:      break;   // sockets: not modelled here
            case LS_SEND_PING:     break;
            case LS_START_ATTEMPT: link_measurement_attempt_begin(); break;
            case LS_END_OK:        link_measurement_attempt_end(true);  break;
            case LS_END_FAIL:      link_measurement_attempt_end(false); break;
            case LS_RESET_XFORM:   link_measurement_reset();            break;
        }
    }
}

// Feed one pong's worth of samples with a given GHostTime, i.e. drive the committed
// intercept to a known place. h_recv == host_time keeps RTT at 0 (the P4-038 filter and the
// LNK-026 gate both pass it untouched), so the intercept lands exactly on ghost_us.
static void feed_pong(int64_t ghost_us) {
    LinkPongFields f = {0};
    f.has_ghost_time = true;  f.ghost_time_us = ghost_us;
    f.has_host_time  = true;  f.host_time_us  = 0;
    link_measurement_add_pong_samples(0, &f);
}

/* ---- trigger ----------------------------------------------------------- */

void test_first_peer_starts_attempt_flush_first(void) {
    int n = start(0xC0A80105, 20808, 1000);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(LS_FLUSH_RX,     acts[0].type);   // LNK-026 bug 2: flush before anything
    TEST_ASSERT_EQUAL_INT(LS_START_ATTEMPT, acts[1].type);
    TEST_ASSERT_EQUAL_INT(LS_SEND_PING,    acts[2].type);
    TEST_ASSERT_FALSE(acts[2].has_prev_ghost);             // round 1: no PrevGHostTime
    TEST_ASSERT_EQUAL_UINT32(0xC0A80105, acts[2].ip);
    TEST_ASSERT_EQUAL_INT(1000, (int)acts[2].send_time_us);
    TEST_ASSERT_TRUE(s.have_ref);
    TEST_ASSERT_EQUAL_INT64(1000 + LINK_SESSION_REMEASURE_US, s.next_measure_us);
    TEST_ASSERT_EQUAL_INT64(1000, s.last_send_us);
}

// ESP-028 changed this contract. It used to assert ZERO actions here — "forget the ref and
// emit nothing" — which is the defect written down as a test: the committed xform stayed
// valid, and the dead session's mapping kept being served as a live phase estimate. Losing
// the peer must now also drop the mapping. See the ESP-028 block below.
void test_no_peer_forgets_ref_and_drops_the_mapping(void) {
    start(0xC0A80105, 20808, 1000);
    int n = link_session_on_trigger(&s, false, 0, 0, 2000, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_RESET_XFORM, acts[0].type);
    TEST_ASSERT_FALSE(s.have_ref);
}

void test_same_peer_not_due_no_retrigger(void) {
    start(0xC0A80105, 20808, 1000);              // next_measure = 1000 + 2e6
    int n = link_session_on_trigger(&s, true, 0xC0A80105, 20808, 5000, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(0, n);                 // same peer, not due
}

void test_due_timer_retriggers(void) {
    start(0xC0A80105, 20808, 1000);
    int64_t due = 1000 + LINK_SESSION_REMEASURE_US;
    int n = link_session_on_trigger(&s, true, 0xC0A80105, 20808, due, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(3, n);                 // periodic re-measure
    TEST_ASSERT_EQUAL_INT(LS_FLUSH_RX, acts[0].type);
}

// ESP-028: a different peer is a different ghost epoch, so the re-target now leads with
// LS_RESET_XFORM (this used to assert 3 actions and no reset).
void test_different_peer_retriggers(void) {
    start(0xC0A80105, 20808, 1000);
    int n = link_session_on_trigger(&s, true, 0xC0A80106, 20808, 1500, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(LS_RESET_XFORM, acts[0].type);
    TEST_ASSERT_EQUAL_UINT32(0xC0A80106, acts[3].ip);
}

void test_trigger_gated_by_active(void) {
    start(0xC0A80105, 20808, 1000);
    int64_t due = 1000 + LINK_SESSION_REMEASURE_US;
    int n = link_session_on_trigger(&s, true, 0xC0A80105, 20808, due, /*active=*/true, acts, 8);
    TEST_ASSERT_EQUAL_INT(0, n);                 // due but an attempt is already running
}

/* ---- ESP-028: the xform must never outlive the endpoint it was measured against ---- */
//
// A Link peer that restarts comes back on a NEW ephemeral mep4 port. LNK-031 re-targeted
// and re-measured, but left the committed GhostXForm MARKED VALID the whole time — and
// link_measurement_have_phase_estimate() is nothing but s_xform.valid. So the old session's
// mapping was served as a live phase estimate: on hardware, `beats` stepped BACKWARDS 276
// beats while /status said sync:1, and (pre-ESP-027) the DIN clock went silent for 138 s.

void test_endpoint_change_resets_xform_before_the_new_attempt(void) {
    start(0xC0A80105, 20808, 1000);

    int n = link_session_on_trigger(&s, true, 0xC0A80105, /*new ephemeral port*/54999,
                                    1500, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(LS_RESET_XFORM,   acts[0].type);  // FIRST — not after the commit
    TEST_ASSERT_EQUAL_INT(LS_FLUSH_RX,      acts[1].type);
    TEST_ASSERT_EQUAL_INT(LS_START_ATTEMPT, acts[2].type);
    TEST_ASSERT_EQUAL_INT(LS_SEND_PING,     acts[3].type);
    TEST_ASSERT_EQUAL_UINT32(54999, acts[3].port);          // ...and aimed at the new port
    TEST_ASSERT_EQUAL_UINT32(54999, s.ref_port);
}

// LINK_SESSION_MAX_ACTIONS is the contract with link_measure_pump's action buffer. The
// emitters are all `if (n < max)`, so a short buffer drops the TAIL — the ping — and the
// attempt silently never leaves the box. Pin the worst case.
void test_retarget_fits_in_max_actions(void) {
    start(0xC0A80105, 20808, 1000);
    int n = link_session_on_trigger(&s, true, 0xC0A80105, 54999, 1500, false,
                                    acts, LINK_SESSION_MAX_ACTIONS);
    TEST_ASSERT_EQUAL_INT(LINK_SESSION_MAX_ACTIONS, n);
    TEST_ASSERT_EQUAL_INT(LS_SEND_PING, acts[n - 1].type);   // the tail survived
}

void test_peer_vanishing_resets_xform_and_arms_immediate_remeasure(void) {
    start(0xC0A80105, 20808, 1000);

    int n = link_session_on_trigger(&s, false, 0, 0, 2000, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_RESET_XFORM, acts[0].type);  // the mapping died with the peer
    TEST_ASSERT_FALSE(s.have_ref);
    TEST_ASSERT_EQUAL_INT64(0, s.next_measure_us);        // measure the instant one returns
}

// Idempotent: the peer stays gone for many polls (PEER_TTL is 15 s, a laptop lid can be
// shut for hours). Re-emitting LS_RESET_XFORM every poll would re-zero the P4-038 phase
// health gauge forever, and the gauge is how we see commits throwing the origin.
void test_peer_absent_resets_once_not_every_poll(void) {
    start(0xC0A80105, 20808, 1000);
    TEST_ASSERT_EQUAL_INT(1, link_session_on_trigger(&s, false, 0, 0, 2000, false, acts, 8));
    TEST_ASSERT_EQUAL_INT(0, link_session_on_trigger(&s, false, 0, 0, 3000, false, acts, 8));
    TEST_ASSERT_EQUAL_INT(0, link_session_on_trigger(&s, false, 0, 0, 9000, false, acts, 8));
}

// Cold start: nothing has ever been committed, so there is nothing to invalidate. Emitting
// a reset here would be harmless to the xform but would wipe the lifetime phase-health
// counters on every boot-time peer discovery.
void test_first_peer_ever_does_not_reset_xform(void) {
    int n = start(0xC0A80105, 20808, 1000);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(LS_FLUSH_RX, acts[0].type);   // no LS_RESET_XFORM
}

// The regression guard on the fix itself: the 2 s periodic re-measure against the SAME
// endpoint must NOT invalidate. Same peer, same session, same ghost epoch — dropping the
// mapping every 2 s would flicker phase invalid forever and re-prime the beat grid each
// time. Only a CHANGED or ABSENT endpoint is a re-origin.
void test_periodic_remeasure_same_peer_keeps_xform(void) {
    start(0xC0A80105, 20808, 1000);
    int64_t due = 1000 + LINK_SESSION_REMEASURE_US;
    int n = link_session_on_trigger(&s, true, 0xC0A80105, 20808, due, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(LS_FLUSH_RX,      acts[0].type);   // no LS_RESET_XFORM
    TEST_ASSERT_EQUAL_INT(LS_START_ATTEMPT, acts[1].type);
    TEST_ASSERT_EQUAL_INT(LS_SEND_PING,     acts[2].type);
}

/* ---- ESP-028: the same episode, replayed against the real link_measurement --------- */

// The property, not the enum: across a kill/rejoin the phase estimate is never served from
// the dead session's mapping. This is the exact ESP-027 hardware episode.
void test_esp027_kill_and_rejoin_never_serves_a_stale_estimate(void) {
    // 1. Peer at :20808. Measure and commit — ghost epoch A.
    run_acts(start(0xC0A80105, 20808, 1000));
    feed_pong(500000);
    run_acts(link_session_on_pong(&s, 1000, true, 500000,
                                  LINK_MEASUREMENT_READY_SAMPLES, acts, 8));
    TEST_ASSERT_TRUE(link_measurement_have_phase_estimate());
    TEST_ASSERT_EQUAL_INT64(500000, link_measurement_current_xform().intercept_us);

    // 2. The peer dies (ByeBye / TTL). This is where /status used to keep saying sync:1.
    run_acts(link_session_on_trigger(&s, false, 0, 0, 2000, false, acts, 8));
    TEST_ASSERT_FALSE(link_measurement_have_phase_estimate());   // <-- the fix

    // 3. It rejoins on a NEW ephemeral port with a NEW ghost epoch (138 s away — the 276
    //    beats at 120 BPM that the analyzer caught). Still no estimate while we re-measure:
    //    a missing estimate free-runs (beat_source), a stale one strands the beat grid.
    run_acts(link_session_on_trigger(&s, true, 0xC0A80105, 54999, 3000, false, acts, 8));
    TEST_ASSERT_FALSE(link_measurement_have_phase_estimate());
    TEST_ASSERT_TRUE(link_measurement_active());

    // 4. The new epoch commits WHOLE. The P4-038 slew clamp (20 ms/commit) would have
    //    fought this if the dead xform were still marked valid — 138 s at 20 ms a commit is
    //    ~3.8 hours of walking. An invalid xform has no origin to protect, so it adopts.
    feed_pong(500000 - 138000000);
    run_acts(link_session_on_pong(&s, 1000, true, 500000 - 138000000,
                                  LINK_MEASUREMENT_READY_SAMPLES, acts, 8));
    TEST_ASSERT_TRUE(link_measurement_have_phase_estimate());
    TEST_ASSERT_EQUAL_INT64(500000 - 138000000,
                            link_measurement_current_xform().intercept_us);
}

// The nastier variant: the rejoin's first measurement FAILS (the peer is up and advertising
// but its responder is not answering yet). LS_END_FAIL leaves a committed xform untouched
// BY DESIGN (ARC-002) — which is precisely why the invalidation has to happen at re-target
// and not at commit. Before the fix, this path served the dead mapping indefinitely.
void test_stale_estimate_not_resurrected_by_a_failed_rejoin_attempt(void) {
    run_acts(start(0xC0A80105, 20808, 1000));
    feed_pong(500000);
    run_acts(link_session_on_pong(&s, 1000, true, 500000,
                                  LINK_MEASUREMENT_READY_SAMPLES, acts, 8));
    TEST_ASSERT_TRUE(link_measurement_have_phase_estimate());

    run_acts(link_session_on_trigger(&s, false, 0, 0, 2000, false, acts, 8));   // peer gone
    run_acts(link_session_on_trigger(&s, true, 0xC0A80105, 54999, 3000, false, acts, 8));

    int64_t t = 3000;                                    // ...and it never answers
    for (int i = 0; i < LINK_SESSION_MAX_TIMEOUTS; i++) {
        t += LINK_SESSION_WATCHDOG_US;
        run_acts(link_session_on_watchdog(&s, t, acts, 8));
    }
    TEST_ASSERT_FALSE(link_measurement_active());                // abandoned
    TEST_ASSERT_FALSE(link_measurement_have_phase_estimate());   // and still not lying
}

/* ---- epoch reset (LNK-026 bug 1) --------------------------------------- */
// ARC-011: the epoch DETECTION + debounce moved to session_timeline
// (test_session_timeline.c). The session just executes a confirmed reset.

void test_epoch_reset_drops_xform_and_rearms(void) {
    start(0xC0A80105, 20808, 1000);                         // have a ref + a schedule
    int n = link_session_on_epoch_reset(&s, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_RESET_XFORM, acts[0].type);
    TEST_ASSERT_FALSE(s.have_ref);                          // forces re-target
    TEST_ASSERT_EQUAL_INT64(0, s.next_measure_us);         // re-measure now
    // ...and the next trigger immediately starts a fresh attempt (due, now >= 0)
    int m = link_session_on_trigger(&s, true, 0xC0A80105, 20808, 100, false, acts, 8);
    TEST_ASSERT_EQUAL_INT(3, m);
}

/* ---- pong -------------------------------------------------------------- */

void test_pong_not_ready_repings_with_prev_ghost(void) {
    start(0xC0A80105, 20808, 1000);
    s.consecutive_timeouts = 3;                              // pretend some silence
    int n = link_session_on_pong(&s, 1200, /*has_ghost=*/true, 999, /*samples=*/1, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_SEND_PING, acts[0].type);
    TEST_ASSERT_TRUE(acts[0].has_prev_ghost);               // round 2+ carries PrevGHostTime
    TEST_ASSERT_EQUAL_INT64(999, acts[0].prev_ghost_us);
    TEST_ASSERT_EQUAL_INT64(1200, acts[0].send_time_us);
    TEST_ASSERT_EQUAL_INT(0, s.consecutive_timeouts);       // pong resets the watchdog count
}

void test_pong_ready_commits(void) {
    start(0xC0A80105, 20808, 1000);
    int n = link_session_on_pong(&s, 1200, true, 999, LINK_MEASUREMENT_READY_SAMPLES, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_END_OK, acts[0].type);         // no re-ping once committed
}

/* ---- watchdog ---------------------------------------------------------- */

void test_watchdog_silent_below_threshold(void) {
    start(0xC0A80105, 20808, 1000);                          // last_send = 1000
    TEST_ASSERT_EQUAL_INT(0, link_session_on_watchdog(&s, 1000 + 40000, acts, 8)); // 40ms < 50
}

void test_watchdog_reping_on_silence(void) {
    start(0xC0A80105, 20808, 1000);
    int n = link_session_on_watchdog(&s, 1000 + LINK_SESSION_WATCHDOG_US, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_SEND_PING, acts[0].type);
    TEST_ASSERT_EQUAL_INT(1, s.consecutive_timeouts);
    TEST_ASSERT_EQUAL_INT64(1000 + LINK_SESSION_WATCHDOG_US, s.last_send_us); // relatched
}

void test_watchdog_abandons_after_max_timeouts(void) {
    start(0xC0A80105, 20808, 1000);
    int64_t t = 1000;
    for (int i = 1; i < LINK_SESSION_MAX_TIMEOUTS; i++) {    // fire 4 re-pings
        t += LINK_SESSION_WATCHDOG_US;
        int n = link_session_on_watchdog(&s, t, acts, 8);
        TEST_ASSERT_EQUAL_INT(1, n);
        TEST_ASSERT_EQUAL_INT(LS_SEND_PING, acts[0].type);
    }
    t += LINK_SESSION_WATCHDOG_US;                           // 5th timeout
    int n = link_session_on_watchdog(&s, t, acts, 8);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(LS_END_FAIL, acts[0].type);        // abandon; glue leaves xform untouched
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_peer_starts_attempt_flush_first);
    RUN_TEST(test_no_peer_forgets_ref_and_drops_the_mapping);
    RUN_TEST(test_same_peer_not_due_no_retrigger);
    RUN_TEST(test_due_timer_retriggers);
    RUN_TEST(test_different_peer_retriggers);
    RUN_TEST(test_trigger_gated_by_active);
    RUN_TEST(test_endpoint_change_resets_xform_before_the_new_attempt);
    RUN_TEST(test_retarget_fits_in_max_actions);
    RUN_TEST(test_peer_vanishing_resets_xform_and_arms_immediate_remeasure);
    RUN_TEST(test_peer_absent_resets_once_not_every_poll);
    RUN_TEST(test_first_peer_ever_does_not_reset_xform);
    RUN_TEST(test_periodic_remeasure_same_peer_keeps_xform);
    RUN_TEST(test_esp027_kill_and_rejoin_never_serves_a_stale_estimate);
    RUN_TEST(test_stale_estimate_not_resurrected_by_a_failed_rejoin_attempt);
    RUN_TEST(test_epoch_reset_drops_xform_and_rearms);
    RUN_TEST(test_pong_not_ready_repings_with_prev_ghost);
    RUN_TEST(test_pong_ready_commits);
    RUN_TEST(test_watchdog_silent_below_threshold);
    RUN_TEST(test_watchdog_reping_on_silence);
    RUN_TEST(test_watchdog_abandons_after_max_timeouts);
    return UNITY_END();
}
