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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_toggle_from_stopped_plays);
    RUN_TEST(test_toggle_from_running_stops);
    RUN_TEST(test_toggle_from_armed_stops);
    return UNITY_END();
}
