// Host tests for the pure quantized-launch state machine (ESP-011). Pressing
// Play mid-bar must not lurch the rig in immediately -- it ARMS, waits for the
// next quantum boundary, and fires MIDI Start exactly on the grid. Stop is
// immediate: musicians expect stop to be instant.
#include "unity.h"
#include "transport_launch.h"

static TransportLaunch tl;
void setUp(void)    { transport_launch_reset(&tl); }
void tearDown(void) {}

#define Q 4.0   // beats per bar

/* ---- arming ------------------------------------------------------------ */

// Press play mid-bar: nothing fires yet, we are armed.
void test_play_mid_bar_arms_and_does_not_fire(void) {
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_PLAY, 2.5, Q, true);
    TEST_ASSERT_EQUAL_INT(TL_NONE, o.action);
    TEST_ASSERT_EQUAL_INT(TL_ARMED, o.state);
}

// Fire exactly when beats cross the next quantum boundary (4.0 here).
void test_armed_fires_on_the_next_quantum_boundary(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 2.5, Q, true);
    TEST_ASSERT_EQUAL_INT(TL_NONE,  transport_launch_step(&tl, TL_INTENT_NONE, 3.9, Q, true).action);
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_NONE, 4.0, Q, true);
    TEST_ASSERT_EQUAL_INT(TL_START, o.action);
    TEST_ASSERT_EQUAL_INT(TL_RUNNING, o.state);
}

// Start fires exactly once; the ticks after the boundary are quiet.
void test_start_fires_once(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 2.5, Q, true);
    transport_launch_step(&tl, TL_INTENT_NONE, 4.0, Q, true);   // START
    TEST_ASSERT_EQUAL_INT(TL_NONE, transport_launch_step(&tl, TL_INTENT_NONE, 4.1, Q, true).action);
    TEST_ASSERT_EQUAL_INT(TL_NONE, transport_launch_step(&tl, TL_INTENT_NONE, 5.0, Q, true).action);
}

// Pressing play exactly ON a boundary must not wait a whole bar.
void test_play_on_the_boundary_fires_immediately(void) {
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_PLAY, 8.0, Q, true);
    TEST_ASSERT_EQUAL_INT(TL_START, o.action);
}

/* ---- no session: nothing to quantize to -------------------------------- */

// Without a beat grid, play starts immediately rather than hanging armed
// forever waiting for a boundary that will never arrive.
void test_play_without_session_starts_immediately(void) {
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_PLAY, 0.0, Q, /*have_beat=*/false);
    TEST_ASSERT_EQUAL_INT(TL_START, o.action);
    TEST_ASSERT_EQUAL_INT(TL_RUNNING, o.state);
}

/* ---- stop -------------------------------------------------------------- */

// Stop is immediate, not quantized.
void test_stop_is_immediate(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 8.0, Q, true);   // running
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_STOP, 8.5, Q, true);
    TEST_ASSERT_EQUAL_INT(TL_STOP, o.action);
    TEST_ASSERT_EQUAL_INT(TL_STOPPED, o.state);
}

// Stop while merely ARMED disarms and emits nothing -- we never started.
void test_stop_while_armed_disarms_silently(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 2.5, Q, true);   // armed
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_STOP, 2.7, Q, true);
    TEST_ASSERT_EQUAL_INT(TL_NONE, o.action);
    TEST_ASSERT_EQUAL_INT(TL_STOPPED, o.state);
    // and the boundary must NOT fire a start
    TEST_ASSERT_EQUAL_INT(TL_NONE, transport_launch_step(&tl, TL_INTENT_NONE, 4.0, Q, true).action);
}

// Stop when already stopped emits nothing (no redundant 0xFC storm).
void test_stop_when_stopped_is_silent(void) {
    TEST_ASSERT_EQUAL_INT(TL_NONE, transport_launch_step(&tl, TL_INTENT_STOP, 1.0, Q, true).action);
}

// Play when already running emits nothing -- no double Start.
void test_play_when_running_is_silent(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 8.0, Q, true);   // running
    TEST_ASSERT_EQUAL_INT(TL_NONE, transport_launch_step(&tl, TL_INTENT_PLAY, 8.5, Q, true).action);
}

/* ---- session loss ------------------------------------------------------ */

// Losing the beat while armed: start immediately rather than hang armed.
void test_beat_lost_while_armed_starts(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 2.5, Q, true);
    TransportLaunchOut o = transport_launch_step(&tl, TL_INTENT_NONE, 2.6, Q, false);
    TEST_ASSERT_EQUAL_INT(TL_START, o.action);
}

/* ---- reset ------------------------------------------------------------- */

void test_reset_returns_to_stopped(void) {
    transport_launch_step(&tl, TL_INTENT_PLAY, 8.0, Q, true);
    transport_launch_reset(&tl);
    TEST_ASSERT_EQUAL_INT(TL_NONE, transport_launch_step(&tl, TL_INTENT_NONE, 12.0, Q, true).action);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_play_mid_bar_arms_and_does_not_fire);
    RUN_TEST(test_armed_fires_on_the_next_quantum_boundary);
    RUN_TEST(test_start_fires_once);
    RUN_TEST(test_play_on_the_boundary_fires_immediately);
    RUN_TEST(test_play_without_session_starts_immediately);
    RUN_TEST(test_stop_is_immediate);
    RUN_TEST(test_stop_while_armed_disarms_silently);
    RUN_TEST(test_stop_when_stopped_is_silent);
    RUN_TEST(test_play_when_running_is_silent);
    RUN_TEST(test_beat_lost_while_armed_starts);
    RUN_TEST(test_reset_returns_to_stopped);
    return UNITY_END();
}
