// Host tests for the pure KitchenSync Touch config (ESP-016): validate + set.
#include "unity.h"
#include "app_config.h"

static AppConfig c;
void setUp(void)    { config_defaults(&c); }
void tearDown(void) {}

void test_defaults_valid(void) {
    TEST_ASSERT_TRUE(config_validate(&c));
    TEST_ASSERT_EQUAL_INT(4, c.quantum_beats);
    TEST_ASSERT_EQUAL_INT(1, c.clock_enable);     // clock on by default
    TEST_ASSERT_EQUAL_INT(0, c.play_on_release);  // touch, not release
    TEST_ASSERT_EQUAL_INT(0, c.nudge_mbeats);     // no clock trim by default
}

// Tracer: a valid quantum is applied.
void test_set_quantum_in_range(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_QUANTUM_BEATS, 8));
    TEST_ASSERT_EQUAL_INT(8, c.quantum_beats);
}

// Out-of-range quantum is rejected, config unchanged. Range is Link beats: the web
// UI edits bars (x4), so 16 bars = 64 beats is the ceiling.
void test_set_quantum_out_of_range(void) {
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_QUANTUM_BEATS, 0));
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_QUANTUM_BEATS, 64));   // 16 bars ok
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_QUANTUM_BEATS, 65));  // past 16 bars
    TEST_ASSERT_EQUAL_INT(64, c.quantum_beats);
}

// Clock nudge: valid within +-250 millibeats, rejected past it.
void test_set_nudge(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_NUDGE_MBEATS, -120));
    TEST_ASSERT_EQUAL_INT(-120, c.nudge_mbeats);
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_NUDGE_MBEATS, 300));  // out of range
    TEST_ASSERT_EQUAL_INT(-120, c.nudge_mbeats);                   // unchanged
}

// Brightness: 10..100 percent; never full-dark (0) or over 100.
void test_set_brightness(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_BRIGHTNESS, 50));
    TEST_ASSERT_EQUAL_INT(50, c.brightness);
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_BRIGHTNESS, 0));    // would be dark
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_BRIGHTNESS, 120));  // over range
    TEST_ASSERT_EQUAL_INT(50, c.brightness);                    // unchanged
}

// Booleans only accept 0/1.
void test_set_bool_fields(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_PLAY_ON_RELEASE, 1));
    TEST_ASSERT_EQUAL_INT(1, c.play_on_release);
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_CLOCK_ENABLE, 2));   // not a bool
    TEST_ASSERT_EQUAL_INT(1, c.clock_enable);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_valid);
    RUN_TEST(test_set_quantum_in_range);
    RUN_TEST(test_set_quantum_out_of_range);
    RUN_TEST(test_set_nudge);
    RUN_TEST(test_set_brightness);
    RUN_TEST(test_set_bool_fields);
    return UNITY_END();
}
