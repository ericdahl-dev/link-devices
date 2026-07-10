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

void setUp(void) {
    ks_tick_reset(&st, 0);
    memset(&cfg, 0, sizeof cfg);
    cfg.clock_out_enable = 1;
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

void test_clock_gated_by_usb_ready(void) {
    TEST_ASSERT_EQUAL_INT(0, run_pulses(false, 1, 1));         // no USB host -> nothing
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_standby_when_session_up_but_stopped);
    RUN_TEST(test_no_standby_while_playing);
    RUN_TEST(test_no_standby_without_session);
    RUN_TEST(test_standby_is_silent);
    RUN_TEST(test_free_run_is_active);
    RUN_TEST(test_cfg_gen_change_reprimes);
    RUN_TEST(test_enabled_output_emits_pulses);
    RUN_TEST(test_clock_gated_by_usb_ready);
    RUN_TEST(test_clock_gated_by_master_switch);
    RUN_TEST(test_disabled_output_never_pulses);
    RUN_TEST(test_metronome_silent_when_stopped);
    RUN_TEST(test_metronome_clicks_when_playing);
    return UNITY_END();
}
