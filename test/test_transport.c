// Host tests for the pure MIDI-transport state machine (P4-008).
#include "unity.h"
#include "transport.h"

static Transport t;
void setUp(void)    { transport_reset(&t); }
void tearDown(void) {}

// An invalid (not-yet-seen) reading never emits and doesn't prime.
void test_invalid_is_noop(void) {
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, transport_update(&t, false, true));
}

// The first valid reading primes silently — no spurious Start on join.
void test_first_valid_primes(void) {
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, transport_update(&t, true, true));
}

void test_start_on_play_edge(void) {
    transport_update(&t, true, false);   // prime: stopped
    TEST_ASSERT_EQUAL_INT(TRANSPORT_START, transport_update(&t, true, true));
}

void test_stop_on_stop_edge(void) {
    transport_update(&t, true, true);    // prime: playing
    TEST_ASSERT_EQUAL_INT(TRANSPORT_STOP, transport_update(&t, true, false));
}

// No repeat while the state is unchanged.
void test_no_repeat_without_change(void) {
    transport_update(&t, true, false);   // prime
    transport_update(&t, true, true);    // START
    TEST_ASSERT_EQUAL_INT(TRANSPORT_NONE, transport_update(&t, true, true));
}

// An invalid gap holds state; the next valid reading still detects the edge.
void test_invalid_holds_state(void) {
    transport_update(&t, true, true);    // prime: playing
    transport_update(&t, false, false);  // gap — ignored
    TEST_ASSERT_EQUAL_INT(TRANSPORT_STOP, transport_update(&t, true, false));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_invalid_is_noop);
    RUN_TEST(test_first_valid_primes);
    RUN_TEST(test_start_on_play_edge);
    RUN_TEST(test_stop_on_stop_edge);
    RUN_TEST(test_no_repeat_without_change);
    RUN_TEST(test_invalid_holds_state);
    return UNITY_END();
}
