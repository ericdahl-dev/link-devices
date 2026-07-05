// Host tests for the pure free-running beat-position accumulator (P4-005).
#include "unity.h"
#include "beat_clock.h"
#include <math.h>

// 120 BPM = 500000 us/beat; 240 BPM = 250000 us/beat.
#define MPB_120  500000
#define MPB_240  250000

static BeatClock b;
void setUp(void)    { beat_clock_reset(&b); }
void tearDown(void) {}

// First advance after reset primes the clock at beat 0 (no jump).
void test_prime_returns_zero(void) {
    TEST_ASSERT_TRUE(fabs(beat_clock_advance(&b, 1000, MPB_120) - 0.0) < 1e-9);
}

// One beat-period of elapsed time advances exactly one beat.
void test_one_period_is_one_beat(void) {
    beat_clock_advance(&b, 0, MPB_120);                       // prime at t=0
    double beats = beat_clock_advance(&b, MPB_120, MPB_120);  // +0.5 s at 120 BPM
    TEST_ASSERT_TRUE(fabs(beats - 1.0) < 1e-9);
}

// A tempo change integrates the next interval at the new rate — smooth, no jump.
void test_tempo_change_is_smooth(void) {
    beat_clock_advance(&b, 0, MPB_120);              // prime, 120 BPM
    beat_clock_advance(&b, MPB_120, MPB_120);        // +1 beat -> 1.0
    double beats = beat_clock_advance(&b, MPB_120 + MPB_240, MPB_240);  // +1 beat at 240 BPM
    TEST_ASSERT_TRUE(fabs(beats - 2.0) < 1e-9);
}

// micros_per_beat <= 0 holds the position (a "no tempo yet" guard).
void test_nonpositive_tempo_holds(void) {
    beat_clock_advance(&b, 0, MPB_120);
    beat_clock_advance(&b, MPB_120, MPB_120);        // -> 1.0
    double beats = beat_clock_advance(&b, MPB_120 * 2, 0);
    TEST_ASSERT_TRUE(fabs(beats - 1.0) < 1e-9);
}

// A backward (non-forward) timestamp holds the position rather than rewinding.
void test_backward_time_holds(void) {
    beat_clock_advance(&b, MPB_120, MPB_120);        // prime at t=500000
    double beats = beat_clock_advance(&b, 0, MPB_120);
    TEST_ASSERT_TRUE(fabs(beats - 0.0) < 1e-9);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_prime_returns_zero);
    RUN_TEST(test_one_period_is_one_beat);
    RUN_TEST(test_tempo_change_is_smooth);
    RUN_TEST(test_nonpositive_tempo_holds);
    RUN_TEST(test_backward_time_holds);
    return UNITY_END();
}
