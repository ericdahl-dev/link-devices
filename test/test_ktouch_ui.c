// Host tests for the pure transport-toggle logic (ESP-016, KitchenSync Touch).
// Touch is transport-ONLY and the whole screen is one toggle -- a tap sends the
// opposite of the current transport state. This is the only pure logic in the
// touch path.
#include "unity.h"
#include "ktouch_ui.h"

void setUp(void) {}
void tearDown(void) {}

// Stopped -> a tap starts (transport_launch then quantizes it to the next bar).
void test_toggle_from_stopped_plays(void) {
    TEST_ASSERT_EQUAL_INT(TL_INTENT_PLAY, ktouch_toggle_intent(TL_STOPPED));
}

// Running -> a tap stops.
void test_toggle_from_running_stops(void) {
    TEST_ASSERT_EQUAL_INT(TL_INTENT_STOP, ktouch_toggle_intent(TL_RUNNING));
}

// Armed (play pressed, waiting for the bar) -> a tap cancels the arm.
void test_toggle_from_armed_stops(void) {
    TEST_ASSERT_EQUAL_INT(TL_INTENT_STOP, ktouch_toggle_intent(TL_ARMED));
}

/* ---- ESP-016: the touch-edge dispatch (was untested glue in ktouch_display) ----
 *
 * Two trigger feels, one decision function:
 *   digital DJ   (play_on_release=0): a press fires the toggle immediately.
 *   turntable DJ (play_on_release=1): a press on a stopped deck CUES it (armed in
 *     hand, no MIDI yet); RELEASING drops it -- transport_launch quantizes the PLAY
 *     to the next "1", so releasing just before the bar lands the beat on the
 *     downbeat. That release is the whole feature.
 */

// TRACER BULLET: the drop. Held (cueing) -> released -> PLAY, and the cue clears.
void test_turntable_release_while_cueing_drops(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_STOPPED, /*touch_ok=*/true, /*touched=*/false,
                                         /*was_touched=*/true, /*cueing=*/true,
                                         /*play_on_release=*/true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_PLAY, o.intent);
    TEST_ASSERT_FALSE(o.cueing);
}

// Turntable: the press only CUES. No MIDI leaves the box until the release -- that
// is what makes it feel like holding a record back.
void test_turntable_press_cues_and_sends_nothing(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_STOPPED, true, true, false, false, true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_NONE, o.intent);
    TEST_ASSERT_TRUE(o.cueing);
}

// Turntable: stop is urgent. Pressing a running deck stops it on the press -- it
// does NOT wait for the release, and it never cues.
void test_turntable_press_on_running_stops_immediately(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_RUNNING, true, true, false, false, true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_STOP, o.intent);
    TEST_ASSERT_FALSE(o.cueing);
}

// Turntable: an armed deck stops on the press too (cancel the pending drop).
void test_turntable_press_on_armed_stops(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_ARMED, true, true, false, false, true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_STOP, o.intent);
}

// Turntable: releasing when nothing was cued does nothing. Lifting off after a STOP
// press must not fire a phantom PLAY.
void test_turntable_release_without_cue_is_silent(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_STOPPED, true, false, true, false, true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_NONE, o.intent);
    TEST_ASSERT_FALSE(o.cueing);
}

// Digital: the press fires the toggle immediately...
void test_digital_press_fires_toggle(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_STOPPED, true, true, false, false, false);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_PLAY, o.intent);
    TEST_ASSERT_FALSE(o.cueing);   // digital never cues
}

// ...and the release is silent, so one tap is exactly one action, not two.
void test_digital_release_is_silent(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_RUNNING, true, false, true, false, false);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_NONE, o.intent);
}

// A held finger repeats nothing. Without this the 5 ms display loop would machine-gun
// the transport for as long as the screen was touched.
void test_held_finger_never_repeats(void) {
    KtouchTouchOut d = ktouch_touch_step(TL_RUNNING, true, /*touched=*/true,
                                         /*was_touched=*/true, false, false);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_NONE, d.intent);

    KtouchTouchOut t = ktouch_touch_step(TL_STOPPED, true, true, true, /*cueing=*/true, true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_NONE, t.intent);
    TEST_ASSERT_TRUE(t.cueing);   // still holding -> still cued
}

// A FAILED sensor read cancels the cue and sends nothing.
//
// Two ways to get this wrong, both bugs:
//  - treat the fault as a release  -> a dropped I2C transaction fires the beat
//  - ignore the fault (the old glue) -> a sensor that stops answering strands the
//    CUE panel on screen forever, with no way back
void test_sensor_fault_cancels_cue_without_firing(void) {
    KtouchTouchOut o = ktouch_touch_step(TL_STOPPED, /*touch_ok=*/false, false,
                                         /*was_touched=*/true, /*cueing=*/true, true);
    TEST_ASSERT_EQUAL_INT(TL_INTENT_NONE, o.intent);   // must NOT drop the beat
    TEST_ASSERT_FALSE(o.cueing);                       // must NOT strand the panel
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_toggle_from_stopped_plays);
    RUN_TEST(test_toggle_from_running_stops);
    RUN_TEST(test_toggle_from_armed_stops);
    RUN_TEST(test_turntable_release_while_cueing_drops);
    RUN_TEST(test_turntable_press_cues_and_sends_nothing);
    RUN_TEST(test_turntable_press_on_running_stops_immediately);
    RUN_TEST(test_turntable_press_on_armed_stops);
    RUN_TEST(test_turntable_release_without_cue_is_silent);
    RUN_TEST(test_digital_press_fires_toggle);
    RUN_TEST(test_digital_release_is_silent);
    RUN_TEST(test_held_finger_never_repeats);
    RUN_TEST(test_sensor_fault_cancels_cue_without_firing);
    return UNITY_END();
}
