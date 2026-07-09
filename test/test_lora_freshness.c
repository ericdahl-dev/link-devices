#include "unity.h"
#include "lora_freshness.h"

void setUp(void)    {}
void tearDown(void) {}

void test_stale_when_never_received(void) {
    TEST_ASSERT_TRUE(lora_freshness_is_stale(10000, 0, 5000, false));
}

void test_not_stale_before_threshold(void) {
    TEST_ASSERT_FALSE(lora_freshness_is_stale(4999, 0, 5000, true));
    TEST_ASSERT_FALSE(lora_freshness_is_stale(6000, 1500, 5000, true));  // elapsed 4500
}

void test_stale_after_threshold_elapsed(void) {
    // boundary: exactly at threshold counts as stale, not strictly greater
    // (mirrors wifi_down_blink_due()'s >= convention)
    TEST_ASSERT_TRUE(lora_freshness_is_stale(5000, 0, 5000, true));
    TEST_ASSERT_TRUE(lora_freshness_is_stale(7000, 1500, 5000, true));  // elapsed 5500
}

void test_stale_calc_survives_millis_rollover(void) {
    uint32_t last = 0xFFFFFFF0u;  // 16 ticks before rollover
    TEST_ASSERT_FALSE(lora_freshness_is_stale(100u, last, 5000, true));   // elapsed 116
    TEST_ASSERT_TRUE(lora_freshness_is_stale(5000u, last, 5000, true));   // elapsed 5016
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stale_when_never_received);
    RUN_TEST(test_not_stale_before_threshold);
    RUN_TEST(test_stale_after_threshold_elapsed);
    RUN_TEST(test_stale_calc_survives_millis_rollover);
    return UNITY_END();
}
