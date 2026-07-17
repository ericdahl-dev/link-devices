#include "unity.h"
#include "transport_button.h"

void setUp(void) {}
void tearDown(void) {}

// A quick press-and-release (held well under the hold threshold) is a TAP — the
// play/stop toggle. Reported on RELEASE, so a press can still become a HOLD.
static void test_quick_press_release_is_a_tap(void) {
    TransportButton tb; transport_button_reset(&tb);
    transport_button_update(&tb, false, 0);                        // seed: released
    transport_button_update(&tb, true, 100);                       // press (bouncing)
    transport_button_update(&tb, true, 100 + BUTTON_DEBOUNCE_MS);  // debounced press edge
    transport_button_update(&tb, true, 200);                       // held briefly (< HOLD_MS)
    transport_button_update(&tb, false, 300);                      // release (bouncing)
    ButtonGesture g = transport_button_update(&tb, false, 300 + BUTTON_DEBOUNCE_MS);  // settled
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_TAP, g);
}

// Holding past the threshold is a HOLD (realign) — fired ONCE the moment it crosses,
// while still down. The release afterward is silent, not a second TAP.
static void test_long_hold_fires_once_then_silent_release(void) {
    TransportButton tb; transport_button_reset(&tb);
    transport_button_update(&tb, false, 0);                        // seed: released
    transport_button_update(&tb, true, 100);                       // press (bouncing)
    transport_button_update(&tb, true, 100 + BUTTON_DEBOUNCE_MS);  // debounced press edge (t=125)
    uint32_t edge = 100 + BUTTON_DEBOUNCE_MS;
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_NONE, transport_button_update(&tb, true, edge + 300));  // < HOLD
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_HOLD, transport_button_update(&tb, true, edge + TRANSPORT_BUTTON_HOLD_MS));
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_NONE, transport_button_update(&tb, true, edge + TRANSPORT_BUTTON_HOLD_MS + 200));  // not again
    transport_button_update(&tb, false, edge + 2000);                       // release (bouncing)
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_NONE, transport_button_update(&tb, false, edge + 2000 + BUTTON_DEBOUNCE_MS));
}

// Back-to-back gestures: a HOLD must not poison the next press. After a hold+release,
// a fresh short press is a clean TAP again (the press edge re-arms the state).
static void test_hold_then_next_press_is_a_clean_tap(void) {
    TransportButton tb; transport_button_reset(&tb);
    transport_button_update(&tb, false, 0);
    transport_button_update(&tb, true, 100);
    uint32_t e1 = 100 + BUTTON_DEBOUNCE_MS;
    transport_button_update(&tb, true, e1);                                       // press edge
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_HOLD, transport_button_update(&tb, true, e1 + TRANSPORT_BUTTON_HOLD_MS));
    transport_button_update(&tb, false, e1 + 1000);                              // release (bouncing)
    transport_button_update(&tb, false, e1 + 1000 + BUTTON_DEBOUNCE_MS);         // settled (silent)
    uint32_t t2 = e1 + 2000;                                                     // second press: a tap
    transport_button_update(&tb, true, t2);
    uint32_t e2 = t2 + BUTTON_DEBOUNCE_MS;
    transport_button_update(&tb, true, e2);                                       // press edge
    transport_button_update(&tb, false, e2 + 100);                               // release (bouncing)
    TEST_ASSERT_EQUAL_INT(BTN_GESTURE_TAP, transport_button_update(&tb, false, e2 + 100 + BUTTON_DEBOUNCE_MS));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_quick_press_release_is_a_tap);
    RUN_TEST(test_long_hold_fires_once_then_silent_release);
    RUN_TEST(test_hold_then_next_press_is_a_clean_tap);
    return UNITY_END();
}
