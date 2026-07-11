// Host tests for the pure per-output clock scheduler (P4-010, swing P4-013).
#include "unity.h"
#include "clock_output.h"

static ClockTicker t;
void setUp(void)    { clock_ticker_reset(&t); }
void tearDown(void) {}

// Straight (no phase, no swing): behaves like clock_ticker at the given ppqn.
void test_straight_matches_ppqn(void) {
    clock_output_due(&t, 0.0, 24, 0, 0, 96);              // prime at beat 0
    int total = 0;
    for (int i = 1; i <= 24; i++)
        total += clock_output_due(&t, i / 24.0, 24, 0, 0, 96);
    TEST_ASSERT_EQUAL_INT(24, total);                     // 24 PPQN over one beat
}

// Division: ppqn=1 emits one pulse per beat.
void test_division_one_per_beat(void) {
    clock_output_due(&t, 0.0, 1, 0, 0, 96);               // prime
    TEST_ASSERT_EQUAL_INT(0, clock_output_due(&t, 0.5, 1, 0, 0, 96));  // same beat
    TEST_ASSERT_EQUAL_INT(1, clock_output_due(&t, 1.0, 1, 0, 0, 96));  // next beat
}

// Phase nudge shifts the grid: +250 milli-beats (+1/4 beat) makes the beat-1
// pulse fire a quarter-beat EARLIER (at real beats 0.75, where straight would
// still be waiting for 1.0).
void test_phase_nudge_fires_earlier(void) {
    clock_output_due(&t, 0.0, 1, 250, 0, 96);             // internally 0.25 -> prime
    TEST_ASSERT_EQUAL_INT(0, clock_output_due(&t, 0.5,  1, 250, 0, 96)); // internally 0.75, slot 0
    TEST_ASSERT_EQUAL_INT(1, clock_output_due(&t, 0.75, 1, 250, 0, 96)); // internally 1.0 -> pulse
}

// Swing delays the off-eighth: at ppqn=2 (pulse on the beat and on the "and"),
// 200 mbeats of swing pushes the "and" pulse from real 0.5 to real 0.7. It has
// NOT fired by 0.6 (straight would have), and fires by 0.7.
void test_swing_delays_offbeat_pulse(void) {
    clock_output_due(&t, 0.0, 2, 0, 200, 96);            // prime at beat 0
    clock_output_due(&t, 0.5, 2, 0, 200, 96);            // straight "and" time -> not yet
    TEST_ASSERT_EQUAL_INT(0, clock_output_due(&t, 0.6, 2, 0, 200, 96)); // still waiting
    TEST_ASSERT_EQUAL_INT(1, clock_output_due(&t, 0.7, 2, 0, 200, 96)); // swung "and" fires
}

// Swing preserves pulse count over a whole beat — the off-eighth is delayed, not
// dropped: ppqn=2 still yields 2 pulses across [0,1].
void test_swing_preserves_count_per_beat(void) {
    clock_output_due(&t, 0.0, 2, 0, 167, 96);            // prime
    int total = 0;
    for (int i = 1; i <= 100; i++)
        total += clock_output_due(&t, i / 100.0, 2, 0, 167, 96);
    TEST_ASSERT_EQUAL_INT(2, total);
}

// --- ARC-019: clock_output_step — the 1ms-writer derivation, one owner -------
// The Touch and X32Link writer tasks each re-typed "if phase invalid: bank
// dropped + reset, else ticks_due(...)" around the pure engine. That rule
// lives here now, behind ClockOutput.

static ClockOutput co;

// Invalid phase is quiet; a valid beat primes silently, then the grid ticks.
void test_step_invalid_primes_then_ticks(void) {
    clock_output_reset(&co);
    TEST_ASSERT_EQUAL_INT(0, clock_output_step(&co, -1.0, 24, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, clock_output_step(&co, 0.0, 24, 0, 0));       // prime
    TEST_ASSERT_EQUAL_INT(1, clock_output_step(&co, 1.0 / 24.0, 24, 0, 0));
}

// Touch-shaped semantics (nudge from config, swing off): step applies the
// nudge exactly like the hand-rolled `beats + nudge/1000.0` it replaces —
// same grid shift test_phase_nudge_fires_earlier proves for clock_output_due.
void test_step_touch_shape_nudge(void) {
    clock_output_reset(&co);
    clock_output_step(&co, 0.0, 1, 250, 0);               // internally 0.25 -> prime
    TEST_ASSERT_EQUAL_INT(0, clock_output_step(&co, 0.5,  1, 250, 0));  // 0.75, slot 0
    TEST_ASSERT_EQUAL_INT(1, clock_output_step(&co, 0.75, 1, 250, 0));  // 1.0 -> fires
}

// The lifetime dropped count survives the invalid-phase reset (the writer
// tasks used to bank this by hand) and only clock_output_reset() zeroes it.
void test_step_banks_dropped_across_invalid(void) {
    clock_output_reset(&co);
    clock_output_step(&co, 0.0, 24, 0, 0);                 // prime at beat 0
    clock_output_step(&co, 2.0, 24, 0, 0);                 // 48-slot jump -> realign
    uint32_t after_jump = clock_output_dropped(&co);
    TEST_ASSERT_TRUE(after_jump > 0);                      // backlog was discarded
    clock_output_step(&co, -1.0, 24, 0, 0);                // invalid: reset banks it
    TEST_ASSERT_EQUAL_UINT32(after_jump, clock_output_dropped(&co));
    clock_output_step(&co, 0.0, 24, 0, 0);                 // re-prime, clean grid
    TEST_ASSERT_EQUAL_UINT32(after_jump, clock_output_dropped(&co));
    clock_output_reset(&co);                               // full clear
    TEST_ASSERT_EQUAL_UINT32(0, clock_output_dropped(&co));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_step_invalid_primes_then_ticks);
    RUN_TEST(test_step_touch_shape_nudge);
    RUN_TEST(test_step_banks_dropped_across_invalid);
    RUN_TEST(test_straight_matches_ppqn);
    RUN_TEST(test_division_one_per_beat);
    RUN_TEST(test_phase_nudge_fires_earlier);
    RUN_TEST(test_swing_delays_offbeat_pulse);
    RUN_TEST(test_swing_preserves_count_per_beat);
    return UNITY_END();
}
