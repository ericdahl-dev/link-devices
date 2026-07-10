#include "unity.h"
#include "battery_gauge.h"

void setUp(void)    {}
void tearDown(void) {}

// 0x9600 (38400) * 78.125uV = 3.0V exactly.
void test_volts_known_value(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, battery_gauge_volts(0x96, 0x00));
}

void test_volts_zero(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, battery_gauge_volts(0x00, 0x00));
}

// MSB is whole percent, LSB is 1/256ths.
void test_percent_whole_number(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 87.0f, battery_gauge_percent(87, 0));
}

void test_percent_fractional(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.5f, battery_gauge_percent(50, 128));  // 128/256 = 0.5
}

// MAX17048 can report a hair over 100% near full charge — pass through, no clamp.
void test_percent_over_100_not_clamped(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 102.0f, battery_gauge_percent(102, 0));
}

void test_parse_fills_both_fields(void) {
    battery_reading_t r;
    TEST_ASSERT_TRUE(battery_gauge_parse(0x96, 0x00, 87, 128, &r));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, r.volts);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 87.5f, r.percent);
}

void test_parse_null_out_returns_false(void) {
    TEST_ASSERT_FALSE(battery_gauge_parse(0x96, 0x00, 87, 128, NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_volts_known_value);
    RUN_TEST(test_volts_zero);
    RUN_TEST(test_percent_whole_number);
    RUN_TEST(test_percent_fractional);
    RUN_TEST(test_percent_over_100_not_clamped);
    RUN_TEST(test_parse_fills_both_fields);
    RUN_TEST(test_parse_null_out_returns_false);
    return UNITY_END();
}
