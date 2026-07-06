#include "unity.h"
#include "wifi_fallback.h"

void setUp(void)    {}
void tearDown(void) {}

// Well before the timeout -> keep retrying STA, don't give up yet.
void test_well_before_timeout_does_not_give_up(void) {
    TEST_ASSERT_FALSE(wifi_fallback_should_give_up(1000000, 0));   // 1s elapsed
    TEST_ASSERT_FALSE(wifi_fallback_should_give_up(29000000, 0));  // 29s elapsed
}

// Zero elapsed (called right as STA starts) -> never give up immediately.
void test_zero_elapsed_does_not_give_up(void) {
    TEST_ASSERT_FALSE(wifi_fallback_should_give_up(0, 0));
    TEST_ASSERT_FALSE(wifi_fallback_should_give_up(5000000, 5000000));
}

// Exactly at the 30s budget -> give up (boundary is inclusive, matching
// X32Link's wifi_try_connect() which times out at millis()-start > 30000
// i.e. gives up once 30s have fully elapsed, not strictly after).
void test_exact_timeout_gives_up(void) {
    TEST_ASSERT_TRUE(wifi_fallback_should_give_up(30000000, 0));
}

// Well past the timeout -> give up.
void test_well_past_timeout_gives_up(void) {
    TEST_ASSERT_TRUE(wifi_fallback_should_give_up(60000000, 0));
}

// Timeout math is relative to connect_start_us, not absolute zero — a
// connect that started at a non-zero boot-relative timestamp must still
// measure elapsed time correctly.
void test_timeout_is_relative_to_connect_start(void) {
    int64_t start = 100000000;  // 100s since boot
    TEST_ASSERT_FALSE(wifi_fallback_should_give_up(start + 29000000, start));
    TEST_ASSERT_TRUE(wifi_fallback_should_give_up(start + 30000000, start));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_well_before_timeout_does_not_give_up);
    RUN_TEST(test_zero_elapsed_does_not_give_up);
    RUN_TEST(test_exact_timeout_gives_up);
    RUN_TEST(test_well_past_timeout_gives_up);
    RUN_TEST(test_timeout_is_relative_to_connect_start);
    return UNITY_END();
}
