// Host tests for the shared pure clock-tick scheduler (LNK-027/LNK-028).
#include "unity.h"
#include "clock_ticker.h"

static ClockTicker ct;
static BarReset    br;
void setUp(void)    { clock_ticker_reset(&ct); bar_reset_reset(&br); }
void tearDown(void) {}

/* ---- ClockTicker ------------------------------------------------------- */

void test_first_call_primes_and_emits_nothing(void) {
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, 10.0, 4, 4));
}

void test_ppqn4_four_ticks_per_beat(void) {
    double slot = 1.0 / 4.0;
    clock_ticker_ticks_due(&ct, 0.0, 4, 4);            // prime at beat 0
    int total = 0;
    for (int i = 1; i <= 4; i++)
        total += clock_ticker_ticks_due(&ct, i * slot, 4, 4);
    TEST_ASSERT_EQUAL_INT(4, total);                   // 4 PPQN over one beat
}

void test_ppqn1_one_tick_per_beat(void) {
    clock_ticker_ticks_due(&ct, 0.0, 1, 4);            // prime
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, 0.5, 1, 4));  // same beat-slot
    TEST_ASSERT_EQUAL_INT(1, clock_ticker_ticks_due(&ct, 1.0, 1, 4));  // next beat
}

void test_ppqn24_matches_midi_cadence(void) {
    double slot = 1.0 / 24.0;
    clock_ticker_ticks_due(&ct, 0.0, 24, 4);           // prime
    int total = 0;
    for (int i = 1; i <= 24; i++)
        total += clock_ticker_ticks_due(&ct, i * slot, 24, 4);
    TEST_ASSERT_EQUAL_INT(24, total);
}

void test_large_jump_reprimes_no_flood(void) {
    clock_ticker_ticks_due(&ct, 0.0, 4, 4);            // prime
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, 100.0, 4, 4));       // realign
    TEST_ASSERT_EQUAL_INT(1, clock_ticker_ticks_due(&ct, 100.0 + 0.25, 4, 4)); // resumes
}

void test_backward_move_emits_nothing(void) {
    clock_ticker_ticks_due(&ct, 5.0, 4, 4);
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, 4.0, 4, 4));
}

void test_negative_beats_and_bad_ppqn(void) {
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, -1.0, 4, 4));  // primes at 0
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, 1.0, 0, 4));   // ppqn<=0 no-op
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&ct, 1.0, -4, 4));
}

/* ---- BarReset ---------------------------------------------------------- */

void test_bar_reset_primes_then_fires_on_crossing(void) {
    TEST_ASSERT_FALSE(bar_reset_due(&br, 0.0, 4.0));   // prime at bar 0
    TEST_ASSERT_FALSE(bar_reset_due(&br, 3.9, 4.0));   // still bar 0
    TEST_ASSERT_TRUE (bar_reset_due(&br, 4.1, 4.0));   // crossed into bar 1
    TEST_ASSERT_FALSE(bar_reset_due(&br, 5.0, 4.0));   // still bar 1
    TEST_ASSERT_TRUE (bar_reset_due(&br, 8.0, 4.0));   // bar 2
}

void test_bar_reset_backward_reprimes_no_fire(void) {
    bar_reset_due(&br, 8.0, 4.0);                      // prime at bar 2
    TEST_ASSERT_FALSE(bar_reset_due(&br, 1.0, 4.0));   // jumped back -> reprime, no false downbeat
    TEST_ASSERT_FALSE(bar_reset_due(&br, 1.5, 4.0));   // same bar 0
    TEST_ASSERT_TRUE (bar_reset_due(&br, 4.0, 4.0));   // clean crossing again
}

void test_bar_reset_multibar_jump_reprimes_no_fire(void) {
    bar_reset_due(&br, 0.0, 4.0);                      // prime bar 0
    TEST_ASSERT_FALSE(bar_reset_due(&br, 40.0, 4.0));  // +10 bars (re-origin) -> reprime, no fire
    TEST_ASSERT_TRUE (bar_reset_due(&br, 44.0, 4.0));  // next clean crossing fires
}

void test_bar_reset_bad_quantum(void) {
    TEST_ASSERT_FALSE(bar_reset_due(&br, 4.0, 0.0));
}

/* ---- ESP-018: the realign path must not discard pulses SILENTLY -----------
 *
 * On a forward jump bigger than max_burst, the ticker deliberately realigns instead of
 * flooding the wire with a catch-up burst -- and `return 0` throws away EVERY pending
 * pulse. That is the right musical call (a 20-pulse burst is worse than a gap), but
 * today it happens with no trace at all: no burst to see on the analyzer, no counter,
 * no log. A stall long enough to trip it is INVISIBLE.
 *
 * The bench caught a ~50ms stall on the Touch's DIN wire only because it was just
 * UNDER the threshold and so produced a visible catch-up burst. A slightly longer one
 * would have vanished. That is the failure that reaches a customer un-diagnosable.
 *
 * So: still realign, but COUNT what was thrown away. `dropped` is health telemetry --
 * it is surfaced in /status on every device and, like usb_midi_batch's counter, it
 * must survive a reset (reset fires whenever Link phase goes invalid, which is normal).
 */
void test_realign_counts_the_pulses_it_discards(void) {
    ClockTicker t; clock_ticker_reset(&t);
    clock_ticker_ticks_due(&t, 0.0, 24, 4);            // prime the grid

    // Jump forward 1 whole beat = 24 ticks, with max_burst 4. The ticker realigns and
    // emits nothing -- 24 pulses' worth of clock never reaches the wire.
    TEST_ASSERT_EQUAL_INT(0, clock_ticker_ticks_due(&t, 1.0, 24, 4));
    TEST_ASSERT_EQUAL_UINT32(24, t.dropped);          // and we KNOW it happened

    // It keeps a running total across events.
    clock_ticker_ticks_due(&t, 3.0, 24, 4);           // another 2-beat jump = 48 ticks
    TEST_ASSERT_EQUAL_UINT32(24 + 48, t.dropped);
}

// A catch-up WITHIN max_burst is delivered, not dropped -- nothing to count.
void test_normal_catchup_drops_nothing(void) {
    ClockTicker t; clock_ticker_reset(&t);
    clock_ticker_ticks_due(&t, 0.0, 24, 4);
    // 3/24 of a beat forward: 3 ticks due, under the burst cap -> all three emitted.
    TEST_ASSERT_EQUAL_INT(3, clock_ticker_ticks_due(&t, 3.0 / 24.0, 24, 4));
    TEST_ASSERT_EQUAL_UINT32(0, t.dropped);
}

// reset() ZEROES the counter. It has to: callers declare the ticker on the stack and
// call reset on it, so a counter reset leaves alone starts as garbage and can never be
// trusted (this test caught exactly that -- it read 25 instead of 24). Keeping a
// lifetime total across resets is the glue's job; a pure struct cannot tell "first
// init" from "re-prime".
void test_reset_zeroes_dropped_so_it_is_always_initialised(void) {
    ClockTicker t; clock_ticker_reset(&t);
    clock_ticker_ticks_due(&t, 0.0, 24, 4);
    clock_ticker_ticks_due(&t, 1.0, 24, 4);           // 24 discarded
    TEST_ASSERT_EQUAL_UINT32(24, t.dropped);

    clock_ticker_reset(&t);
    TEST_ASSERT_EQUAL_UINT32(0, t.dropped);           // zeroed -> never garbage
    TEST_ASSERT_FALSE(t.primed);                      // and the grid is re-primed
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_call_primes_and_emits_nothing);
    RUN_TEST(test_ppqn4_four_ticks_per_beat);
    RUN_TEST(test_ppqn1_one_tick_per_beat);
    RUN_TEST(test_ppqn24_matches_midi_cadence);
    RUN_TEST(test_large_jump_reprimes_no_flood);
    RUN_TEST(test_backward_move_emits_nothing);
    RUN_TEST(test_negative_beats_and_bad_ppqn);
    RUN_TEST(test_bar_reset_primes_then_fires_on_crossing);
    RUN_TEST(test_bar_reset_backward_reprimes_no_fire);
    RUN_TEST(test_bar_reset_multibar_jump_reprimes_no_fire);
    RUN_TEST(test_bar_reset_bad_quantum);
    RUN_TEST(test_realign_counts_the_pulses_it_discards);
    RUN_TEST(test_normal_catchup_drops_nothing);
    RUN_TEST(test_reset_zeroes_dropped_so_it_is_always_initialised);
    return UNITY_END();
}
