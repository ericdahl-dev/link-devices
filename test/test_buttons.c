#include "unity.h"
#include "buttons.h"

void setUp(void) {}
void tearDown(void) {}

// Held down at boot must NOT read as a press — otherwise a stuck button (or a
// finger on the switch during power-up) fires an action on every reset.
static void test_held_at_boot_is_not_a_press(void) {
    Button b;
    button_reset(&b);
    TEST_ASSERT_FALSE(button_update(&b, true, 0));
    TEST_ASSERT_TRUE(button_is_pressed(&b));          // it IS down, just not an edge
    TEST_ASSERT_FALSE(button_update(&b, true, 1000)); // and stays not-an-edge
}

static void test_clean_press_fires_once_after_debounce(void) {
    Button b;
    button_reset(&b);
    button_update(&b, false, 0);                      // seed: released

    TEST_ASSERT_FALSE(button_update(&b, true, 100));  // pressed, not settled yet
    TEST_ASSERT_FALSE(button_update(&b, true, 100 + BUTTON_DEBOUNCE_MS - 1));
    TEST_ASSERT_TRUE(button_update(&b, true, 100 + BUTTON_DEBOUNCE_MS));  // settled -> edge

    // held: no repeat
    TEST_ASSERT_FALSE(button_update(&b, true, 500));
    TEST_ASSERT_FALSE(button_update(&b, true, 5000));
    TEST_ASSERT_TRUE(button_is_pressed(&b));
}

// The whole point of the module: contact bounce must not produce N presses.
static void test_bounce_produces_exactly_one_press(void) {
    Button b;
    button_reset(&b);
    button_update(&b, false, 0);

    // 10 ms of chatter — every transition restarts the settle window
    TEST_ASSERT_FALSE(button_update(&b, true,  100));
    TEST_ASSERT_FALSE(button_update(&b, false, 102));
    TEST_ASSERT_FALSE(button_update(&b, true,  104));
    TEST_ASSERT_FALSE(button_update(&b, false, 107));
    TEST_ASSERT_FALSE(button_update(&b, true,  110));

    // settle window runs from the LAST transition (110), not the first (100)
    TEST_ASSERT_FALSE(button_update(&b, true, 110 + BUTTON_DEBOUNCE_MS - 1));
    TEST_ASSERT_TRUE(button_update(&b, true, 110 + BUTTON_DEBOUNCE_MS));

    // ...and only one press came out of all that chatter
    TEST_ASSERT_FALSE(button_update(&b, true, 200));
}

static void test_release_is_not_a_press_and_rearms(void) {
    Button b;
    button_reset(&b);
    button_update(&b, false, 0);
    button_update(&b, true, 100);
    TEST_ASSERT_TRUE(button_update(&b, true, 100 + BUTTON_DEBOUNCE_MS));

    // release: debounced, but never reports an edge
    TEST_ASSERT_FALSE(button_update(&b, false, 300));
    TEST_ASSERT_FALSE(button_update(&b, false, 300 + BUTTON_DEBOUNCE_MS));
    TEST_ASSERT_FALSE(button_is_pressed(&b));

    // second press fires again
    TEST_ASSERT_FALSE(button_update(&b, true, 500));
    TEST_ASSERT_TRUE(button_update(&b, true, 500 + BUTTON_DEBOUNCE_MS));
}

// A glitch shorter than the debounce window must be swallowed entirely.
static void test_glitch_shorter_than_window_is_ignored(void) {
    Button b;
    button_reset(&b);
    button_update(&b, false, 0);

    TEST_ASSERT_FALSE(button_update(&b, true,  100));
    TEST_ASSERT_FALSE(button_update(&b, false, 100 + BUTTON_DEBOUNCE_MS - 1));
    TEST_ASSERT_FALSE(button_update(&b, false, 1000));
    TEST_ASSERT_FALSE(button_is_pressed(&b));
}

// millis() wraps every ~49.7 days. The subtraction must survive it, or a rig left
// on the bench for a month stops responding to its buttons.
static void test_survives_millis_wrap(void) {
    Button b;
    button_reset(&b);
    button_update(&b, false, UINT32_MAX - 10);

    TEST_ASSERT_FALSE(button_update(&b, true, UINT32_MAX - 5));
    // now_ms wraps past zero; (now - since) must still compute 25
    TEST_ASSERT_TRUE(button_update(&b, true, (uint32_t)(UINT32_MAX - 5 + BUTTON_DEBOUNCE_MS)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_held_at_boot_is_not_a_press);
    RUN_TEST(test_clean_press_fires_once_after_debounce);
    RUN_TEST(test_bounce_produces_exactly_one_press);
    RUN_TEST(test_release_is_not_a_press_and_rearms);
    RUN_TEST(test_glitch_shorter_than_window_is_ignored);
    RUN_TEST(test_survives_millis_wrap);
    return UNITY_END();
}
