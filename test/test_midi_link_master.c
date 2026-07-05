// Host tests for the pure P4-011 MIDI->Link master timeline builder.
#include "unity.h"
#include "midi_link_master.h"
#include "link_protocol.h"

void setUp(void)    {}
void tearDown(void) {}

// Evaluate a timeline's beat at time t (Link's toBeats), in beats.
static double beat_at(const LinkTimeline* tl, int64_t t) {
    double b0 = (double)tl->beat_origin_micro / 1.0e6;
    return b0 + (double)(t - tl->time_origin_us) / (double)tl->micros_per_beat;
}

void test_tempo_from_bpm(void) {
    LinkTimeline tl = midi_link_master_timeline(120.0, 0, 1000000, NULL, false);
    TEST_ASSERT_EQUAL_INT64(500000, tl.micros_per_beat);
}

void test_no_session_anchors_beat_from_pulses(void) {
    // 48 pulses = 2 beats; now = 1_000_000 us.
    LinkTimeline tl = midi_link_master_timeline(120.0, 48, 1000000, NULL, false);
    TEST_ASSERT_EQUAL_INT64(2000000, tl.beat_origin_micro);   // 2.0 beats
    TEST_ASSERT_EQUAL_INT64(1000000, tl.time_origin_us);
    // beat at now == 2.0
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, (float)beat_at(&tl, 1000000));
}

// Taking over a running session: preserve the beat at `now`, adopt our tempo,
// and outrank the observed beatOrigin.
void test_takeover_preserves_beat_and_outranks(void) {
    LinkTimeline obs = { .micros_per_beat = 600000LL,    // 100 BPM
                         .beat_origin_micro = 10000000LL, // 10.0 beats
                         .time_origin_us    = 0LL };
    // now = 6_000_000 us -> observed beat = 10 + 6e6/6e5 = 20.0
    int64_t now = 6000000;
    LinkTimeline tl = midi_link_master_timeline(120.0, 999, now, &obs, true);

    TEST_ASSERT_EQUAL_INT64(500000, tl.micros_per_beat);     // our tempo (120)
    // continuity: our timeline reads the same beat (20.0) at now
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 20.0f, (float)beat_at(&tl, now));
    // priority: beatOrigin strictly exceeds the observed one
    TEST_ASSERT_TRUE(tl.beat_origin_micro > obs.beat_origin_micro);
}

// Degenerate case: now == observed timeOrigin so current beat == observed
// beatOrigin. The +1 floor must still make us strictly outrank it.
void test_takeover_strict_increase_when_beats_equal(void) {
    LinkTimeline obs = { .micros_per_beat = 500000LL,
                         .beat_origin_micro = 100000000LL,  // 100.0 beats
                         .time_origin_us    = 5000000LL };
    LinkTimeline tl = midi_link_master_timeline(120.0, 0, 5000000 /* == timeOrigin */,
                                                &obs, true);
    TEST_ASSERT_TRUE(tl.beat_origin_micro > obs.beat_origin_micro);
    TEST_ASSERT_EQUAL_INT64(101000000, tl.beat_origin_micro);  // 100 + 1
}

// Republishing as the session advances yields an ever-increasing beatOrigin, so
// each broadcast keeps winning adoption.
void test_republish_beatorigin_monotonic(void) {
    LinkTimeline obs = { .micros_per_beat = 500000LL,
                         .beat_origin_micro = 0LL,
                         .time_origin_us    = 0LL };
    LinkTimeline a = midi_link_master_timeline(120.0, 0, 1000000, &obs, true);
    LinkTimeline b = midi_link_master_timeline(120.0, 0, 2000000, &obs, true);
    TEST_ASSERT_TRUE(b.beat_origin_micro > a.beat_origin_micro);
}

void test_zero_bpm_yields_zero_tempo(void) {
    LinkTimeline tl = midi_link_master_timeline(0.0, 0, 0, NULL, false);
    TEST_ASSERT_EQUAL_INT64(0, tl.micros_per_beat);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tempo_from_bpm);
    RUN_TEST(test_no_session_anchors_beat_from_pulses);
    RUN_TEST(test_takeover_preserves_beat_and_outranks);
    RUN_TEST(test_takeover_strict_increase_when_beats_equal);
    RUN_TEST(test_republish_beatorigin_monotonic);
    RUN_TEST(test_zero_bpm_yields_zero_tempo);
    return UNITY_END();
}
