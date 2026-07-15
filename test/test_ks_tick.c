// Host tests for the pure clock-tick orchestration (ARC-015). The deep modules it
// drives are tested elsewhere; this covers the orchestration between them — the
// reprime fold, the per-output fan-out gating, and the transport/metronome gating.
// Uses free-run mode (have_session=true + no committed xform) so beats advance with t.
#include "unity.h"
#include "ks_tick.h"
#include <string.h>

#define MPB 500000   // micros/beat, 120 bpm

static KsTickState st;
static KsConfig    cfg;
static LinkTimeline TL;

static LinkGhostXForm no_xform(void) { LinkGhostXForm x; memset(&x, 0, sizeof x); x.valid = false; return x; }

/* ---- ESP-038: emission order is an invariant owned by the plan ---------- */
// The one rule that caused ESP-023: on a START tick, the 0xFA must be emitted
// BEFORE the first 0xF8, or a slave starting on this downbeat is one tick late.
static int idx_of(const uint8_t* buf, int n, uint8_t b) {
    for (int i = 0; i < n; i++) if (buf[i] == b) return i;
    return -1;
}

void test_start_byte_precedes_first_clock(void) {
    KsTickPlan p;
    memset(&p, 0, sizeof p);
    p.transport[0] = TRANSPORT_START;
    p.pulses[0]    = 3;

    uint8_t buf[8];
    int n = ks_tick_out_bytes(&p, 0, buf, sizeof buf);

    int fa = idx_of(buf, n, 0xFA);
    int f8 = idx_of(buf, n, 0xF8);
    TEST_ASSERT_EQUAL_INT(4, n);                 // 1 start + 3 clocks
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fa);     // start present
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, f8);     // clock present
    TEST_ASSERT_LESS_THAN_INT(f8, fa);           // START strictly before first CLOCK
}

void test_none_emits_only_clocks(void) {
    KsTickPlan p; memset(&p, 0, sizeof p);
    p.transport[0] = TRANSPORT_NONE;
    p.pulses[0]    = 2;
    uint8_t buf[8];
    int n = ks_tick_out_bytes(&p, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(-1, idx_of(buf, n, 0xFA));
    TEST_ASSERT_EQUAL_INT(-1, idx_of(buf, n, 0xFC));
}

void test_stop_emits_fc(void) {
    KsTickPlan p; memset(&p, 0, sizeof p);
    p.transport[0] = TRANSPORT_STOP;
    uint8_t buf[8];
    int n = ks_tick_out_bytes(&p, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX8(0xFC, buf[0]);
}

void test_empty_plan_emits_nothing(void) {
    KsTickPlan p; memset(&p, 0, sizeof p);   // NONE + 0 pulses
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_INT(0, ks_tick_out_bytes(&p, 0, buf, sizeof buf));
}

void test_cap_never_overflows(void) {
    KsTickPlan p; memset(&p, 0, sizeof p);
    p.transport[0] = TRANSPORT_START;
    p.pulses[0]    = 96;                      // KS_TICK_MAX_BURST
    uint8_t buf[3];
    int n = ks_tick_out_bytes(&p, 0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(3, n);              // clamped to cap, no overflow
    TEST_ASSERT_EQUAL_HEX8(0xFA, buf[0]);     // order still honored under clamp
}

void setUp(void) {
    ks_tick_reset(&st, 0);
    memset(&cfg, 0, sizeof cfg);
    cfg.clock_out_enable = 1;
    // memset zeroes follow_link, but the REAL default is 1 (Link owns transport).
    // Model the default here or every test silently becomes a manual-output test.
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) cfg.clock[i].follow_link = 1;
    cfg.clock[0].enable = 1; cfg.clock[0].cable = 0; cfg.clock[0].ppqn = 1;   // 1 pulse/beat
    cfg.metronome_enable = 1; cfg.metronome_accent = 1;
    memset(&TL, 0, sizeof TL);
    TL.micros_per_beat = MPB;
}
void tearDown(void) {}

static KsTickInputs mk(int64_t t_now, uint32_t gen, bool tp_playing, bool usb_ready) {
    KsTickInputs in; memset(&in, 0, sizeof in);
    in.have_session = true; in.xform = no_xform(); in.tl = TL; in.t_now = t_now;
    in.cfg = &cfg; in.cfg_gen = gen; in.tp_playing = tp_playing;
    in.start_stop_seen = false; in.playing = true; in.usb_ready = usb_ready;
    return in;
}

void test_free_run_is_active(void) {
    KsTickInputs in = mk(0, 0, true, true);
    TEST_ASSERT_TRUE(ks_tick_step(&st, &in).active);
}

// ESP-015: plan.downbeat fires exactly once per bar (KS_TICK_METRO_QUANTUM beats),
// on the crossing into a new bar. The analyzer triggers on this to measure the
// 0xFA-vs-downbeat offset (ESP-011). The first active tick primes and does not fire.
void test_downbeat_fires_once_per_bar(void) {
    int fires = 0, first_active_fired = -1;
    // Two bars, in quarter-beat steps. beats == t_now/MPB in free-run.
    for (int k = 0; k <= 8 * 4; k++) {
        KsTickInputs in = mk((int64_t)k * (MPB / 4), 0, true, true);
        KsTickPlan p = ks_tick_step(&st, &in);
        if (k == 0) first_active_fired = p.downbeat ? 1 : 0;
        if (p.downbeat) fires++;
    }
    TEST_ASSERT_EQUAL_INT(0, first_active_fired);   // priming tick never fires
    TEST_ASSERT_EQUAL_INT(2, fires);                // one per bar crossing (bars 1 and 2)
}

// A stopped/no-session tick must not emit a downbeat: no grid to sit on.
void test_no_downbeat_without_session(void) {
    KsTickInputs in; memset(&in, 0, sizeof in);
    in.have_session = false; in.xform = no_xform(); in.tl = TL;
    in.cfg = &cfg; in.usb_ready = true;
    TEST_ASSERT_FALSE(ks_tick_step(&st, &in).downbeat);
}

void test_cfg_gen_change_reprimes(void) {
    KsTickInputs a = mk(MPB, 0, true, true);  ks_tick_step(&st, &a);   // settle at gen 0
    KsTickInputs b = mk(2*MPB, 1, true, true);                          // gen bumped
    TEST_ASSERT_TRUE(ks_tick_step(&st, &b).reprime);
}

// Sum pulses on output 0 across a few beats of stepping.
static int run_pulses(bool usb_ready, bool clock_enable, int out_enable) {
    cfg.clock_out_enable = clock_enable;
    cfg.clock[0].enable  = out_enable;
    int total = 0;
    for (int k = 0; k <= 8; k++) {
        KsTickInputs in = mk((int64_t)k * (MPB / 4), 0, true, usb_ready);  // quarter-beat steps
        KsTickPlan p = ks_tick_step(&st, &in);
        total += p.pulses[0];
    }
    return total;
}

void test_enabled_output_emits_pulses(void) {
    TEST_ASSERT_GREATER_THAN_INT(0, run_pulses(true, 1, 1));   // usb ready, clock on, out on
}

// P4-039: plan.bpm derives from the settled timeline the caller already holds
// (in->tl), not a separate value that a caller could zero out independently —
// so a frozen/held timeline (peer loss, session still settled) keeps reporting
// its real tempo instead of drifting to whatever a stale side-channel says.
void test_bpm_reflects_locked_timeline(void) {
    KsTickInputs in = mk(0, 0, true, true);   // TL.micros_per_beat == MPB == 500000 (120 bpm)
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, p.bpm);
}

void test_bpm_zero_without_session(void) {
    KsTickInputs in; memset(&in, 0, sizeof in);
    in.have_session = false; in.xform = no_xform(); in.tl = TL;
    in.cfg = &cfg; in.usb_ready = true;
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.bpm);
}

// P4-039: once the session has told us it's playing, losing the peer (which
// resets in->start_stop_seen back to false, per link_protocol.c) must not snap
// the reported transport state back to "not playing" — hold the last known value.
void test_playing_held_across_peer_loss(void) {
    KsTickInputs in = mk(0, 0, true, true);
    in.start_stop_seen = true; in.playing = true;
    ks_tick_step(&st, &in);   // session says playing

    in = mk(MPB, 0, true, true);
    in.start_stop_seen = false; in.playing = false;   // peer gone: link_proto zeroed both
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_TRUE(p.playing);
}

// A device that has never seen a StartStopState (no session has ever reported
// transport) must not fabricate "playing" — nothing to hold yet.
void test_playing_false_when_never_seen(void) {
    KsTickInputs in = mk(0, 0, true, true);
    in.start_stop_seen = false; in.playing = false;
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_FALSE(p.playing);
}

// ESP-015: pulses are a musical decision, NOT gated on a USB host -- the DIN MIDI
// output must clock with no USB device attached. (The USB *send* is gated in the
// glue, not here.) This replaces the old "no USB -> no pulses" behavior.
void test_clock_not_gated_by_usb_ready(void) {
    TEST_ASSERT_GREATER_THAN_INT(0, run_pulses(false, 1, 1));  // no USB host -> DIN still clocks
}

void test_clock_gated_by_master_switch(void) {
    TEST_ASSERT_EQUAL_INT(0, run_pulses(true, 0, 1));          // clock_out_enable off
}

void test_disabled_output_never_pulses(void) {
    // out 1 is disabled throughout; assert it stays silent while out 0 runs.
    cfg.clock[1].enable = 0; cfg.clock[1].ppqn = 1;
    bool any1 = false;
    for (int k = 0; k <= 8; k++) {
        KsTickInputs in = mk((int64_t)k * (MPB / 4), 0, true, true);
        if (ks_tick_step(&st, &in).pulses[1] != 0) any1 = true;
    }
    TEST_ASSERT_FALSE(any1);
}

void test_metronome_silent_when_stopped(void) {
    bool clicked = false;
    for (int k = 0; k <= 12; k++) {
        KsTickInputs in = mk((int64_t)k * (MPB / 4), 0, /*tp_playing=*/false, true);
        if (ks_tick_step(&st, &in).click) clicked = true;
    }
    TEST_ASSERT_FALSE(clicked);   // transport stopped -> no click
}

void test_metronome_clicks_when_playing(void) {
    bool clicked = false;
    for (int k = 0; k <= 12; k++) {
        KsTickInputs in = mk((int64_t)k * (MPB / 4), 0, /*tp_playing=*/true, true);
        if (ks_tick_step(&st, &in).click) clicked = true;
    }
    TEST_ASSERT_TRUE(clicked);
}

/* ---- ESP-009: standby -- connected-but-stopped must not look dead ------- */

// Session up, transport stopped: the plan says "standby" so the caller can show
// a heartbeat instead of going dark and indistinguishable from a crashed board.
void test_standby_when_session_up_but_stopped(void) {
    KsTickInputs in = mk(0, 0, /*tp_playing=*/false, true);
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_TRUE(p.active);
    TEST_ASSERT_TRUE(p.standby);
}

// Playing: no standby, the beat chase owns the strip.
void test_no_standby_while_playing(void) {
    KsTickInputs in = mk(0, 0, /*tp_playing=*/true, true);
    TEST_ASSERT_FALSE(ks_tick_step(&st, &in).standby);
}

// No session at all is NOT standby -- that state is "disconnected", visually
// distinct, and owned by the caller (WiFi-down blink convention).
void test_no_standby_without_session(void) {
    KsTickInputs in = mk(0, 0, false, true);
    in.have_session = false;
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_FALSE(p.active);
    TEST_ASSERT_FALSE(p.standby);
}

// Standby never clicks the speaker. An idle rig must stay silent.
void test_standby_is_silent(void) {
    KsTickInputs in = mk(0, 0, false, true);
    TEST_ASSERT_FALSE(ks_tick_step(&st, &in).click);
}

/* ---- ESP-011: per-output transport ------------------------------------- */

// Each output owns its own transport. Playing output 0 leaves output 1 stopped.
// beats=0 is a bar line, so output 0's START fires on this very tick.
void test_transport_is_per_output(void) {
    cfg.clock[1].enable = 1; cfg.clock[1].ppqn = 1;
    KsTickInputs in = mk(0, 0, true, true);
    in.launch[0] = TL_INTENT_PLAY;            // only output 0 asked to play
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_EQUAL_INT(TRANSPORT_START, p.transport[0]);   // beats=0 -> on the grid
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE,  p.transport[1]);
}

// Play mid-bar arms; START lands on the next bar line, not immediately.
void test_play_mid_bar_fires_on_the_downbeat(void) {
    // Prime the free-run accumulator: its first advance always returns beat 0,
    // which is a bar line -- press play there and it fires instantly (correctly).
    KsTickInputs p0 = mk(0, 0, true, true);
    ks_tick_step(&st, &p0);

    KsTickInputs a = mk((int64_t)(2.5 * MPB), 0, true, true);
    a.launch[0] = TL_INTENT_PLAY;
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, ks_tick_step(&st, &a).transport[0]);

    KsTickInputs b = mk((int64_t)(3.9 * MPB), 0, true, true);
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, ks_tick_step(&st, &b).transport[0]);

    KsTickInputs c = mk((int64_t)(4.0 * MPB), 0, true, true);   // bar line
    TEST_ASSERT_EQUAL_INT(TRANSPORT_START, ks_tick_step(&st, &c).transport[0]);
}

// Stop is immediate and per-output.
void test_stop_is_immediate_per_output(void) {
    KsTickInputs a = mk(0, 0, true, true);
    a.launch[0] = TL_INTENT_PLAY;
    ks_tick_step(&st, &a);                                   // output 0 running
    KsTickInputs b = mk((int64_t)(1.5 * MPB), 0, true, true);
    b.launch[0] = TL_INTENT_STOP;
    TEST_ASSERT_EQUAL_INT(TRANSPORT_STOP, ks_tick_step(&st, &b).transport[0]);
}

// A disabled output never emits transport, whatever it is asked for.
void test_disabled_output_emits_no_transport(void) {
    cfg.clock[0].enable = 0;
    KsTickInputs in = mk(0, 0, true, true);
    in.launch[0] = TL_INTENT_PLAY;
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, ks_tick_step(&st, &in).transport[0]);
}

// Transport state must not depend on a USB device being plugged in. Found on
// hardware: with no device, pressing Play never even armed, because the launch
// step sat inside the usb_ready gate. Pulses need the cable; state does not.
void test_launch_tracks_without_usb(void) {
    KsTickInputs in = mk(0, 0, true, /*usb_ready=*/false);
    in.launch[0] = TL_INTENT_PLAY;
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_EQUAL_INT(TL_RUNNING, p.launch_state[0]);   // beats=0 -> bar line
    TEST_ASSERT_EQUAL_INT(0, p.pulses[0]);                  // but no clock without USB
}

// Arbitration (ESP-011 option 1): once the session publishes transport, Link
// owns it and manual presses are ignored. Found on hardware: without this the
// manual START fired and the session's stopped state stomped it in the SAME
// millisecond -- "transport out1 START" immediately followed by "STOP".
void test_session_transport_wins_over_manual(void) {
    KsTickInputs in = mk(0, 0, true, true);
    in.start_stop_seen = true;    // Ableton has Start/Stop sync on
    in.playing = false;           // ...and is stopped
    in.launch[0] = TL_INTENT_PLAY;
    KsTickPlan p = ks_tick_step(&st, &in);
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, p.transport[0]);   // no START at all
    TEST_ASSERT_EQUAL_INT(TL_STOPPED, p.launch_state[0]);    // and not armed
}

// With no session transport published, manual owns it.
void test_manual_owns_transport_when_session_silent(void) {
    KsTickInputs in = mk(0, 0, true, true);
    in.start_stop_seen = false;
    in.launch[0] = TL_INTENT_PLAY;
    TEST_ASSERT_EQUAL_INT(TRANSPORT_START, ks_tick_step(&st, &in).transport[0]);
}

// Per-output transport ownership: an output set to MANUAL (follow_link=0) is not
// stomped by the Link session's transport. This is the entire reason the flag
// exists -- the bench bug was a manual START killed by the session's stopped state
// in the same millisecond. The arbitration's invariant is "exactly one master per
// output", so here the session must NOT be that master.
void test_manual_output_survives_session_stop(void) {
    cfg.clock[0].follow_link = 0;   // this output is ours, not Link's

    // Prime the beat grid first: with no grid, transport_launch deliberately starts
    // Play immediately rather than hanging armed forever, which wouldn't exercise
    // the quantized path we care about here.
    int64_t t = 0;
    KsTickPlan p;
    for (int i = 0; i < 10; i++) {
        t += 20000;
        KsTickInputs w = mk(t, 0, true, true);
        w.start_stop_seen = true; w.playing = true;
        ks_tick_step(&st, &w);
    }

    // Session is publishing transport and IS playing. Press manual play.
    KsTickInputs in = mk(t, 0, true, true);
    in.start_stop_seen = true; in.playing = true;
    in.launch[0] = TL_INTENT_PLAY;      // arms; fires on the next bar line
    p = ks_tick_step(&st, &in);

    bool started = (p.transport[0] == TRANSPORT_START);
    for (int i = 0; i < 400; i++) {
        t += 20000;
        in = mk(t, 0, true, true);
        in.start_stop_seen = true; in.playing = true;
        p = ks_tick_step(&st, &in);
        if (p.transport[0] == TRANSPORT_START) started = true;
    }
    TEST_ASSERT_TRUE(started);
    TEST_ASSERT_EQUAL_INT(TL_RUNNING, p.launch_state[0]);

    // Now the SESSION stops. Our manual output must keep running, un-stomped.
    for (int i = 0; i < 50; i++) {
        t += 20000;
        in = mk(t, 0, true, true);
        in.start_stop_seen = true; in.playing = false;   // session stopped
        p = ks_tick_step(&st, &in);
        TEST_ASSERT_NOT_EQUAL(TRANSPORT_STOP, p.transport[0]);
    }
    TEST_ASSERT_EQUAL_INT(TL_RUNNING, p.launch_state[0]);
}

// Independence (ESP-011's whole point): the session's transport reaches the outputs
// that follow it and NOT the manual ones, so a drum machine can track Ableton while
// a synth waits to be launched by hand.
void test_session_start_reaches_only_following_outputs(void) {
    cfg.clock[0].enable = 1; cfg.clock[0].follow_link = 1;   // follows Link
    cfg.clock[1].enable = 1; cfg.clock[1].ppqn = 1;
    cfg.clock[1].follow_link = 0;                            // manual — ours

    // The session must TRANSITION stopped->playing: transport_update primes on its
    // first observation and emits nothing (joining a session mid-play must not jump
    // gear to bar 1, P4-008). So observe it stopped first, then start it.
    int64_t t = 0;
    KsTickPlan p; bool out0_started = false, out1_started = false;
    for (int i = 0; i < 10; i++) {
        t += 20000;
        KsTickInputs w = mk(t, 0, true, true);
        w.start_stop_seen = true; w.playing = false;   // session present but stopped
        ks_tick_step(&st, &w);
    }
    for (int i = 0; i < 200; i++) {
        t += 20000;
        KsTickInputs in = mk(t, 0, true, true);
        in.start_stop_seen = true; in.playing = true;  // session starts playing
        p = ks_tick_step(&st, &in);
        if (p.transport[0] == TRANSPORT_START) out0_started = true;
        if (p.transport[1] == TRANSPORT_START) out1_started = true;
    }
    TEST_ASSERT_TRUE(out0_started);    // Link owns it -> session START lands
    TEST_ASSERT_FALSE(out1_started);   // manual -> session must NOT start it
}

// A manual output stops the moment you ask, even while the session plays on.
void test_manual_stop_is_immediate_while_session_plays(void) {
    cfg.clock[0].follow_link = 0;

    int64_t t = 0;
    KsTickPlan p;
    for (int i = 0; i < 10; i++) {   // prime the grid
        t += 20000;
        KsTickInputs w = mk(t, 0, true, true);
        w.start_stop_seen = true; w.playing = true;
        ks_tick_step(&st, &w);
    }
    KsTickInputs in = mk(t, 0, true, true);
    in.start_stop_seen = true; in.playing = true;
    in.launch[0] = TL_INTENT_PLAY;
    ks_tick_step(&st, &in);
    for (int i = 0; i < 400; i++) {           // let it reach RUNNING
        t += 20000;
        in = mk(t, 0, true, true);
        in.start_stop_seen = true; in.playing = true;
        p = ks_tick_step(&st, &in);
    }
    TEST_ASSERT_EQUAL_INT(TL_RUNNING, p.launch_state[0]);

    // Manual STOP: immediate, not quantized -- session is still playing.
    t += 20000;
    in = mk(t, 0, true, true);
    in.start_stop_seen = true; in.playing = true;
    in.launch[0] = TL_INTENT_STOP;
    p = ks_tick_step(&st, &in);
    TEST_ASSERT_EQUAL_INT(TRANSPORT_STOP, p.transport[0]);
    TEST_ASSERT_EQUAL_INT(TL_STOPPED, p.launch_state[0]);
}

// P4-038: a re-prime ZEROES each ClockTicker's own `dropped` (a pure struct cannot tell
// first-init from re-prime, so clock_ticker.c says banking the lifetime total is the
// caller's job -- ARC-019 does exactly that inside ClockOutput for the 1 ms writers).
// KitchenSync keeps its own burst policy (KS_TICK_MAX_BURST 96: prefer catch-up over
// dropping), so it needs the same banking HERE, in the pure step, not in the glue.
void test_dropped_total_survives_a_reprime(void) {
    KsTickState st;
    ks_tick_reset(&st, 0);
    st.cts[0].dropped = 12;           // the ticker discarded 12 pulses on a realign
    st.cts[1].dropped = 5;

    ks_tick_bank_dropped(&st);        // a re-prime is about to zero them
    TEST_ASSERT_EQUAL_UINT32(17, ks_tick_dropped(&st));

    st.cts[0].dropped = 0;            // ...and it did
    st.cts[1].dropped = 0;
    TEST_ASSERT_EQUAL_UINT32(17, ks_tick_dropped(&st));   // lifetime total survives

    st.cts[0].dropped = 3;            // new drops accumulate on top of the banked total
    TEST_ASSERT_EQUAL_UINT32(20, ks_tick_dropped(&st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dropped_total_survives_a_reprime);
    RUN_TEST(test_manual_output_survives_session_stop);
    RUN_TEST(test_session_start_reaches_only_following_outputs);
    RUN_TEST(test_manual_stop_is_immediate_while_session_plays);
    RUN_TEST(test_session_transport_wins_over_manual);
    RUN_TEST(test_manual_owns_transport_when_session_silent);
    RUN_TEST(test_launch_tracks_without_usb);
    RUN_TEST(test_transport_is_per_output);
    RUN_TEST(test_play_mid_bar_fires_on_the_downbeat);
    RUN_TEST(test_stop_is_immediate_per_output);
    RUN_TEST(test_disabled_output_emits_no_transport);
    RUN_TEST(test_standby_when_session_up_but_stopped);
    RUN_TEST(test_no_standby_while_playing);
    RUN_TEST(test_no_standby_without_session);
    RUN_TEST(test_standby_is_silent);
    RUN_TEST(test_free_run_is_active);
    RUN_TEST(test_downbeat_fires_once_per_bar);
    RUN_TEST(test_no_downbeat_without_session);
    RUN_TEST(test_cfg_gen_change_reprimes);
    RUN_TEST(test_enabled_output_emits_pulses);
    RUN_TEST(test_bpm_reflects_locked_timeline);
    RUN_TEST(test_bpm_zero_without_session);
    RUN_TEST(test_playing_held_across_peer_loss);
    RUN_TEST(test_playing_false_when_never_seen);
    RUN_TEST(test_clock_not_gated_by_usb_ready);
    RUN_TEST(test_clock_gated_by_master_switch);
    RUN_TEST(test_disabled_output_never_pulses);
    RUN_TEST(test_metronome_silent_when_stopped);
    RUN_TEST(test_metronome_clicks_when_playing);
    RUN_TEST(test_start_byte_precedes_first_clock);
    RUN_TEST(test_none_emits_only_clocks);
    RUN_TEST(test_stop_emits_fc);
    RUN_TEST(test_empty_plan_emits_nothing);
    RUN_TEST(test_cap_never_overflows);
    return UNITY_END();
}
