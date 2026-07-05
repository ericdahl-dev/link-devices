// Host tests for the pure metronome scheduler (P4-006). Decides, from the
// monotonic beat position, WHEN to click and WHETHER a click is a bar-downbeat
// accent. Composes the shared clock_ticker (ppqn=1 -> one pulse/beat) and
// BarReset (bar boundary) engines unchanged — the audio tone burst is glue.
#include "unity.h"
#include "metronome.h"

static Metronome m;
void setUp(void)    { metronome_reset(&m); }
void tearDown(void) {}

#define BURST 8

// First observation aligns the grid and emits nothing (no startup click).
void test_first_update_primes_silent(void) {
    TEST_ASSERT_EQUAL_INT(METRO_NONE, metronome_update(&m, 0.0, 4.0, BURST));
}

// One click per whole beat; nothing between beats.
void test_click_once_per_beat(void) {
    metronome_update(&m, 0.0, 4.0, BURST);                                  // prime
    TEST_ASSERT_EQUAL_INT(METRO_NONE,  metronome_update(&m, 0.25, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_NONE,  metronome_update(&m, 0.75, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK, metronome_update(&m, 1.0,  4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_NONE,  metronome_update(&m, 1.5,  4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK, metronome_update(&m, 2.0,  4.0, BURST));
}

// Beats 1..3 are plain clicks; the bar boundary (beat 4, 8, ...) accents.
void test_accent_on_bar_downbeat(void) {
    metronome_update(&m, 0.0, 4.0, BURST);                                  // prime (bar-0 downbeat swallowed)
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 1.0, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 2.0, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 3.0, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_ACCENT, metronome_update(&m, 4.0, 4.0, BURST));  // bar 1
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 5.0, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 6.0, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 7.0, 4.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_ACCENT, metronome_update(&m, 8.0, 4.0, BURST));  // bar 2
}

// The accent cadence follows the quantum (e.g. 3/4 -> every 3 beats).
void test_accent_follows_quantum(void) {
    metronome_update(&m, 0.0, 3.0, BURST);                                  // prime
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 1.0, 3.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 2.0, 3.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_ACCENT, metronome_update(&m, 3.0, 3.0, BURST));  // bar 1 of a 3/4 bar
    TEST_ASSERT_EQUAL_INT(METRO_CLICK,  metronome_update(&m, 4.0, 3.0, BURST));
    TEST_ASSERT_EQUAL_INT(METRO_ACCENT, metronome_update(&m, 6.0, 3.0, BURST));
}

// A big forward jump (tempo re-origin / stall) re-primes instead of flooding —
// no click backlog, and the first accent after realign lands on the next clean
// bar boundary rather than a false one.
void test_large_jump_reprimes_no_flood(void) {
    metronome_update(&m, 0.0, 4.0, BURST);                                  // prime
    TEST_ASSERT_EQUAL_INT(METRO_NONE,  metronome_update(&m, 100.0,  4.0, BURST));  // realign, no burst
    TEST_ASSERT_EQUAL_INT(METRO_NONE,  metronome_update(&m, 100.25, 4.0, BURST));  // same beat slot
    TEST_ASSERT_EQUAL_INT(METRO_CLICK, metronome_update(&m, 101.0,  4.0, BURST));  // resumes on next beat
}

// A backward move (never expected from a monotonic clock, but be safe) is silent.
void test_backward_move_silent(void) {
    metronome_update(&m, 5.0, 4.0, BURST);                                  // prime
    TEST_ASSERT_EQUAL_INT(METRO_NONE, metronome_update(&m, 4.0, 4.0, BURST));
}

// reset() re-arms priming so the next update is silent again.
void test_reset_rearms_priming(void) {
    metronome_update(&m, 0.0, 4.0, BURST);
    metronome_update(&m, 1.0, 4.0, BURST);
    metronome_reset(&m);
    TEST_ASSERT_EQUAL_INT(METRO_NONE, metronome_update(&m, 50.0, 4.0, BURST));  // primes fresh, silent
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_update_primes_silent);
    RUN_TEST(test_click_once_per_beat);
    RUN_TEST(test_accent_on_bar_downbeat);
    RUN_TEST(test_accent_follows_quantum);
    RUN_TEST(test_large_jump_reprimes_no_flood);
    RUN_TEST(test_backward_move_silent);
    RUN_TEST(test_reset_rearms_priming);
    return UNITY_END();
}
