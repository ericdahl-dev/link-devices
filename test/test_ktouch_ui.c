// Host tests for the pure transport-button hit-test (ESP-016, KitchenSync Touch).
// Touch is transport-ONLY (locked constraint): two buttons, PLAY and STOP, no
// settings screen. This is the only pure logic in the touch path.
#include "unity.h"
#include "ktouch_ui.h"

void setUp(void) {}
void tearDown(void) {}

// Tracer: a tap inside the PLAY button reports PLAY.
void test_tap_in_play_reports_play(void) {
    TEST_ASSERT_EQUAL_INT(KTOUCH_BTN_PLAY, ktouch_ui_hit(80, 210));
}

void test_tap_in_stop_reports_stop(void) {
    TEST_ASSERT_EQUAL_INT(KTOUCH_BTN_STOP, ktouch_ui_hit(80, 285));
}

// The BPM/status area up top is not a button -- a tap there does nothing (no
// accidental transport, and there is no settings screen to reach at all).
void test_tap_in_status_area_reports_none(void) {
    TEST_ASSERT_EQUAL_INT(KTOUCH_BTN_NONE, ktouch_ui_hit(80, 40));
}

// The gap between the two buttons is dead space -- prevents a fat-finger on the
// PLAY/STOP border from firing the wrong one.
void test_tap_in_gap_reports_none(void) {
    TEST_ASSERT_EQUAL_INT(KTOUCH_BTN_NONE, ktouch_ui_hit(80, 254));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tap_in_play_reports_play);
    RUN_TEST(test_tap_in_stop_reports_stop);
    RUN_TEST(test_tap_in_status_area_reports_none);
    RUN_TEST(test_tap_in_gap_reports_none);
    return UNITY_END();
}
