// Host tests for the pure MIDI-clock-IN tracker (P4-011).
#include "unity.h"
#include "midi_clock_in.h"

void setUp(void)    { midi_clock_in_reset(); }
void tearDown(void) {}

// Feed `n` pulses spaced `interval_us` apart starting at t0; returns the last ts.
static int64_t feed(int n, int64_t t0, int64_t interval_us) {
    int64_t t = t0;
    for (int i = 0; i < n; i++) { midi_clock_in_pulse(t); t += interval_us; }
    return t - interval_us;   // timestamp of the last pulse fed
}

void test_zero_before_window_filled(void) {
    // 120 BPM: a 24-PPQN interval = 500000/24 us. One short of WINDOW -> no BPM.
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW - 1, 0, 500000 / 24);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, midi_clock_in_bpm(last));
}

void test_120_bpm(void) {
    int64_t iv = 500000 / 24;                 // 120 BPM at 24 PPQN
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW, 0, iv);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, midi_clock_in_bpm(last));
}

void test_60_bpm(void) {
    int64_t iv = 1000000 / 24;                // 60 BPM
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW, 100000, iv);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 60.0f, midi_clock_in_bpm(last));
}

// The ring keeps only the last WINDOW: after a tempo change, BPM reflects the
// newest window, not the stale early pulses.
void test_tracks_latest_window(void) {
    feed(MIDI_CLOCK_IN_WINDOW, 0, 1000000 / 24);   // start at 60 BPM
    int64_t t0 = MIDI_CLOCK_IN_WINDOW * (int64_t)(1000000 / 24);
    int64_t iv = 500000 / 24;                       // switch to 120 BPM
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW, t0, iv);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, midi_clock_in_bpm(last));
}

void test_timeout_returns_zero(void) {
    int64_t iv = 500000 / 24;
    int64_t last = feed(MIDI_CLOCK_IN_WINDOW, 0, iv);
    // Query far in the future -> clock considered stopped.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f,
                             midi_clock_in_bpm(last + MIDI_CLOCK_IN_TIMEOUT_US + 1));
}

void test_pulse_count_accumulates(void) {
    feed(30, 0, 1000);
    TEST_ASSERT_EQUAL_UINT32(30, midi_clock_in_pulse_count());
    feed(12, 40000, 1000);
    TEST_ASSERT_EQUAL_UINT32(42, midi_clock_in_pulse_count());
}

void test_reset_clears_state(void) {
    feed(MIDI_CLOCK_IN_WINDOW, 0, 500000 / 24);
    midi_clock_in_reset();
    TEST_ASSERT_EQUAL_UINT32(0, midi_clock_in_pulse_count());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, midi_clock_in_bpm(0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_before_window_filled);
    RUN_TEST(test_120_bpm);
    RUN_TEST(test_60_bpm);
    RUN_TEST(test_tracks_latest_window);
    RUN_TEST(test_timeout_returns_zero);
    RUN_TEST(test_pulse_count_accumulates);
    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
