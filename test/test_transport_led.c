// Host tests for the illuminated-button lamp logic (ESP-025). The blink is the only
// thing that makes a quantized button legible: press Play mid-bar and nothing happens
// for up to a bar, so the lamp has to say "armed" or the user presses again.
#include "unity.h"
#include "transport_led.h"

void setUp(void) {}
void tearDown(void) {}

#define B TRANSPORT_LED_BLINK_MS

static void test_stopped_is_dark(void) {
    TEST_ASSERT_FALSE(transport_led_on(TL_STOPPED, 0));
    TEST_ASSERT_FALSE(transport_led_on(TL_STOPPED, B));
    TEST_ASSERT_FALSE(transport_led_on(TL_STOPPED, 12345));
}

static void test_running_is_solid(void) {
    // Solid means solid — not on for half the blink period.
    TEST_ASSERT_TRUE(transport_led_on(TL_RUNNING, 0));
    TEST_ASSERT_TRUE(transport_led_on(TL_RUNNING, B));
    TEST_ASSERT_TRUE(transport_led_on(TL_RUNNING, B + 1));
    TEST_ASSERT_TRUE(transport_led_on(TL_RUNNING, 12345));
}

static void test_armed_blinks(void) {
    TEST_ASSERT_TRUE(transport_led_on(TL_ARMED, 0));
    TEST_ASSERT_TRUE(transport_led_on(TL_ARMED, B - 1));
    TEST_ASSERT_FALSE(transport_led_on(TL_ARMED, B));        // first dark half
    TEST_ASSERT_FALSE(transport_led_on(TL_ARMED, 2 * B - 1));
    TEST_ASSERT_TRUE(transport_led_on(TL_ARMED, 2 * B));     // lit again
}

static void test_realign_lamp_only_when_armed(void) {
    TEST_ASSERT_FALSE(realign_led_on(false, 0));
    TEST_ASSERT_FALSE(realign_led_on(false, B));
    TEST_ASSERT_FALSE(realign_led_on(false, 12345));

    TEST_ASSERT_TRUE(realign_led_on(true, 0));
    TEST_ASSERT_FALSE(realign_led_on(true, B));
    TEST_ASSERT_TRUE(realign_led_on(true, 2 * B));
}

// Both lamps must blink in phase — two lit buttons blinking against each other
// on the same panel reads as a fault, not as two armed actions.
static void test_both_lamps_blink_in_phase(void) {
    for (uint32_t t = 0; t < 6 * B; t += B / 3) {
        TEST_ASSERT_EQUAL_INT(transport_led_on(TL_ARMED, t), realign_led_on(true, t));
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stopped_is_dark);
    RUN_TEST(test_running_is_solid);
    RUN_TEST(test_armed_blinks);
    RUN_TEST(test_realign_lamp_only_when_armed);
    RUN_TEST(test_both_lamps_blink_in_phase);
    return UNITY_END();
}
