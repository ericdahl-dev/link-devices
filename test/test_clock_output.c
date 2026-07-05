// Host tests for the pure per-output clock scheduler (P4-010).
#include "unity.h"
#include "clock_output.h"

static ClockTicker t;
void setUp(void)    { clock_ticker_reset(&t); }
void tearDown(void) {}

// Straight (no phase): behaves like clock_ticker at the given ppqn.
void test_straight_matches_ppqn(void) {
    clock_output_due(&t, 0.0, 24, 0, 96);              // prime at beat 0
    int total = 0;
    for (int i = 1; i <= 24; i++)
        total += clock_output_due(&t, i / 24.0, 24, 0, 96);
    TEST_ASSERT_EQUAL_INT(24, total);                  // 24 PPQN over one beat
}

// Division: ppqn=1 emits one pulse per beat.
void test_division_one_per_beat(void) {
    clock_output_due(&t, 0.0, 1, 0, 96);               // prime
    TEST_ASSERT_EQUAL_INT(0, clock_output_due(&t, 0.5, 1, 0, 96));  // same beat
    TEST_ASSERT_EQUAL_INT(1, clock_output_due(&t, 1.0, 1, 0, 96));  // next beat
}

// Phase nudge shifts the grid: +250 milli-beats (+1/4 beat) makes the beat-1
// pulse fire a quarter-beat EARLIER (at real beats 0.75, where straight would
// still be waiting for 1.0).
void test_phase_nudge_fires_earlier(void) {
    clock_output_due(&t, 0.0, 1, 250, 96);             // internally 0.25 -> prime
    TEST_ASSERT_EQUAL_INT(0, clock_output_due(&t, 0.5,  1, 250, 96)); // internally 0.75, slot 0
    TEST_ASSERT_EQUAL_INT(1, clock_output_due(&t, 0.75, 1, 250, 96)); // internally 1.0 -> pulse
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_straight_matches_ppqn);
    RUN_TEST(test_division_one_per_beat);
    RUN_TEST(test_phase_nudge_fires_earlier);
    return UNITY_END();
}
