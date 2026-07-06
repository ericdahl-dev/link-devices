// Host tests for the pure MIDI-clock-IN tracker (P4-011). It rings the last
// window of 24-PPQN pulse timestamps and derives BPM via the reused, host-tested
// midi_bpm_calc — plus a "not enough pulses yet" and a "clock stopped" guard.
#include "unity.h"
#include "midi_clock_in.h"
#include <math.h>

void setUp(void)    { midi_clock_in_reset(); }
void tearDown(void) {}

// Feed `n` pulses spaced `dt` us apart starting at t=0; returns the last ts.
static int64_t feed(int n, int64_t dt) {
    int64_t t = 0;
    for (int i = 0; i < n; i++) { midi_clock_in_pulse(t); t += dt; }
    return t - dt;
}

// 24 PPQN at 20000 us/pulse is exactly 125 BPM (60e6 / (125*24) = 20000).
void test_bpm_from_steady_clock(void) {
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW, 20000);
    TEST_ASSERT_TRUE(fabsf(midi_clock_in_bpm(last) - 125.0f) < 0.5f);
}

// Below a full window there isn't a beat's worth of intervals yet -> 0.
void test_needs_full_window(void) {
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW - 1, 20000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, midi_clock_in_bpm(last));
}

// Every pulse bumps the running 24-PPQN count (beat = count/24).
void test_pulse_count_runs(void) {
    feed(50, 20000);
    TEST_ASSERT_EQUAL_UINT32(50, midi_clock_in_pulse_count());
}

// No pulse for longer than the timeout -> the clock is considered stopped (0).
void test_clock_stop_zeroes_bpm(void) {
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW, 20000);
    TEST_ASSERT_TRUE(midi_clock_in_bpm(last) > 0.0f);                       // running
    TEST_ASSERT_EQUAL_FLOAT(0.0f, midi_clock_in_bpm(last + MIDI_CLOCK_IN_TIMEOUT_US + 1));
}

// Reset clears the count and BPM.
void test_reset_clears(void) {
    feed(30, 20000);
    midi_clock_in_reset();
    TEST_ASSERT_EQUAL_UINT32(0, midi_clock_in_pulse_count());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, midi_clock_in_bpm(0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bpm_from_steady_clock);
    RUN_TEST(test_needs_full_window);
    RUN_TEST(test_pulse_count_runs);
    RUN_TEST(test_clock_stop_zeroes_bpm);
    RUN_TEST(test_reset_clears);
    return UNITY_END();
}
