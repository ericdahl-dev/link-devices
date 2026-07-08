// Host tests for the pure settled-timeline owner + epoch debounce (ARC-011).
// These carry the P4-028 debounce regressions, now owned here instead of LinkSession.
#include "unity.h"
#include "session_timeline.h"

static SessionTimeline st;
void setUp(void)    { session_timeline_reset(&st); }
void tearDown(void) {}

static LinkTimeline tl(int64_t origin) {
    LinkTimeline t;
    t.micros_per_beat   = 500000;   // 120 bpm
    t.beat_origin_micro = 0;
    t.time_origin_us    = origin;
    return t;
}

void test_settled_false_before_first_observe(void) {
    LinkTimeline out;
    TEST_ASSERT_FALSE(session_timeline_settled(&st, &out));
}

void test_first_observe_primes_settled_no_reset(void) {
    TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(67000000), 0));
    LinkTimeline out;
    TEST_ASSERT_TRUE(session_timeline_settled(&st, &out));
    TEST_ASSERT_EQUAL_INT64(67000000, out.time_origin_us);
}

void test_forward_step_tracks_no_reset(void) {
    session_timeline_observe(&st, true, tl(1000000), 0);
    TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(1500000), 100));   // forward
    LinkTimeline out; session_timeline_settled(&st, &out);
    TEST_ASSERT_EQUAL_INT64(1500000, out.time_origin_us);
}

void test_invalid_is_noop(void) {
    session_timeline_observe(&st, true, tl(5000000), 0);
    TEST_ASSERT_FALSE(session_timeline_observe(&st, false, tl(0), 100));
    LinkTimeline out; session_timeline_settled(&st, &out);
    TEST_ASSERT_EQUAL_INT64(5000000, out.time_origin_us);   // settled untouched
}

void test_genuine_reorigin_confirms_after_settle(void) {
    session_timeline_observe(&st, true, tl(510000000), 0);                  // prime
    // First sight of the backward jump: pending, held — not a reset yet.
    TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(200000), 1000));
    LinkTimeline out; session_timeline_settled(&st, &out);
    TEST_ASSERT_EQUAL_INT64(510000000, out.time_origin_us);                 // still holding good
    // Persisting past the window: confirmed.
    TEST_ASSERT_TRUE(session_timeline_observe(&st, true, tl(200000),
                                              1000 + SESSION_TIMELINE_SETTLE_US));
    session_timeline_settled(&st, &out);
    TEST_ASSERT_EQUAL_INT64(200000, out.time_origin_us);                    // adopted new epoch
}

void test_transient_join_no_reset_and_holds_good(void) {
    session_timeline_observe(&st, true, tl(67000000), 0);                   // good session
    // Junk from a joining peer: pending, held — beat math keeps the good timeline.
    TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(544860), 1000));
    LinkTimeline out; session_timeline_settled(&st, &out);
    TEST_ASSERT_EQUAL_INT64(67000000, out.time_origin_us);                  // NOT junk
    // Correction within the window clears the candidate.
    TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(67000000), 1000 + 100000));
    // Long after, still no lingering reset.
    TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(67500000), 1000 + 2000000));
}

void test_intermittent_junk_never_confirms(void) {
    session_timeline_observe(&st, true, tl(67000000), 0);
    for (int i = 0; i < 10; i++) {
        int64_t t = 1000 + (int64_t)i * 400000;
        TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(544860), t));
        TEST_ASSERT_FALSE(session_timeline_observe(&st, true, tl(67000000), t + 50000));
    }
    LinkTimeline out; session_timeline_settled(&st, &out);
    TEST_ASSERT_EQUAL_INT64(67000000, out.time_origin_us);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_settled_false_before_first_observe);
    RUN_TEST(test_first_observe_primes_settled_no_reset);
    RUN_TEST(test_forward_step_tracks_no_reset);
    RUN_TEST(test_invalid_is_noop);
    RUN_TEST(test_genuine_reorigin_confirms_after_settle);
    RUN_TEST(test_transient_join_no_reset_and_holds_good);
    RUN_TEST(test_intermittent_junk_never_confirms);
    return UNITY_END();
}
