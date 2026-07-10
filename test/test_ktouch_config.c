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
}

// Tracer: a valid quantum is applied.
void test_set_quantum_in_range(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_QUANTUM_BEATS, 8));
    TEST_ASSERT_EQUAL_INT(8, c.quantum_beats);
}

// Out-of-range quantum is rejected, config unchanged.
void test_set_quantum_out_of_range(void) {
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_QUANTUM_BEATS, 0));
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_QUANTUM_BEATS, 17));
    TEST_ASSERT_EQUAL_INT(4, c.quantum_beats);   // still the default
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
    RUN_TEST(test_set_bool_fields);
    return UNITY_END();
}
