// Host tests for the pure swing/shuffle time-warp (P4-013). swing_warp pushes the
// off-eighth of each beat later so a straight clock grooves, while keeping whole
// beats fixed (no pulses gained/lost). swing is expressed in milli-beats of delay
// applied to the n+0.5 boundary: 0 = straight, up to 250 => the boundary at 0.75.
#include "unity.h"
#include "swing.h"
#include <math.h>

#define NEAR(exp, act) TEST_ASSERT_TRUE(fabs((act) - (exp)) < 1e-9)

void setUp(void) {}
void tearDown(void) {}

// Zero (or negative) swing is a straight passthrough.
void test_zero_swing_is_identity(void) {
    NEAR(0.0,  swing_warp(0.0,  0));
    NEAR(0.37, swing_warp(0.37, 0));
    NEAR(3.5,  swing_warp(3.5,  0));
    NEAR(1.25, swing_warp(1.25, -10));   // guard: negative treated as straight
}

// Whole beats are fixed points for any swing — the warp maps each beat onto
// itself, so pulse count per beat is preserved.
void test_beat_boundaries_are_fixed(void) {
    NEAR(0.0, swing_warp(0.0, 200));
    NEAR(1.0, swing_warp(1.0, 200));
    NEAR(4.0, swing_warp(4.0, 167));
}

// The off-eighth (grid 0.5) is reached at real position 0.5 + swing/1000: with
// 200 mbeats of swing, the "and" of the beat lands at 0.70 instead of 0.50.
void test_offbeat_pushed_later(void) {
    // real 0.70 maps to grid 0.50 (the off-eighth boundary)
    NEAR(0.5, swing_warp(0.70, 200));
    NEAR(1.5, swing_warp(1.70, 200));   // same within any beat
    // a straight clock would already be past the off-eighth at real 0.60...
    TEST_ASSERT_TRUE(swing_warp(0.60, 200) < 0.5);   // ...but swung, it's not there yet
}

// Warp is monotonic increasing (a count-based ticker needs a forward-moving
// input or it would re-prime).
void test_monotonic(void) {
    double prev = swing_warp(0.0, 200);
    for (int i = 1; i <= 400; i++) {
        double cur = swing_warp(i / 100.0, 200);
        TEST_ASSERT_TRUE(cur > prev);
        prev = cur;
    }
}

// Continuous at the swing boundary: approaching real=0.5+swing from below and
// above both give grid 0.5.
void test_continuous_at_boundary(void) {
    double ratio = 0.5 + 175.0 / 1000.0;   // 0.675
    NEAR(0.5, swing_warp(ratio, 175));
    TEST_ASSERT_TRUE(fabs(swing_warp(ratio - 1e-6, 175) - 0.5) < 1e-3);
    TEST_ASSERT_TRUE(fabs(swing_warp(ratio + 1e-6, 175) - 0.5) < 1e-3);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_swing_is_identity);
    RUN_TEST(test_beat_boundaries_are_fixed);
    RUN_TEST(test_offbeat_pushed_later);
    RUN_TEST(test_monotonic);
    RUN_TEST(test_continuous_at_boundary);
    return UNITY_END();
}
