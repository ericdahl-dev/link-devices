// Host tests for the pure mic tempo detector (P4-020, "Follow Beat" v1:
// detect + display only). follow_beat owns envelope extraction (rectify +
// one-pole lowpass, decimated to a fixed analysis rate) and autocorrelation-
// based BPM estimation over a rolling window. No I2S/ESP-IDF dependency --
// follow_beat_io.c feeds it raw mic samples at the native sample rate.
#include "unity.h"
#include "follow_beat.h"
#include <math.h>
#include <stdlib.h>

static FollowBeat f;
void setUp(void)    { follow_beat_reset(&f); }
void tearDown(void) {}

// Feed n_samples of a synthetic click train: a short decaying impulse every
// `period_samples`, near-silence otherwise (small fixed dither, not zero --
// real mic input is never a flat line, and the envelope's one-pole lowpass
// should reject dither as noise floor either way). Returns the last output.
static FollowBeatOut feed_click_train(int period_samples, int n_samples) {
    FollowBeatOut out = {0};
    for (int i = 0; i < n_samples; i++) {
        int16_t s = (i % period_samples == 0) ? 20000 : ((i % 7 == 0) ? 40 : 0);
        out = follow_beat_push_sample(&f, s);
    }
    return out;
}

// Before the rolling window fills (< FOLLOW_BEAT_ENV_WINDOW_S seconds of
// audio), there's not enough data for a real estimate.
void test_not_valid_before_window_fills(void) {
    // 8000 samples at 16kHz = 0.5s -- well short of the 4s window.
    FollowBeatOut out = feed_click_train(8000, 8000);
    TEST_ASSERT_FALSE(out.valid);
}

// 120 BPM: one beat every 0.5s = 8000 samples at 16kHz. After a few full
// windows (10s of audio), the detector should converge on ~120 BPM with
// enough confidence to report valid.
void test_detects_120_bpm(void) {
    FollowBeatOut out = feed_click_train(8000, 16000 * 10);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - 120.0f) < 3.0f);
}

// 90 BPM: one beat every 0.6667s = 10667 samples at 16kHz.
void test_detects_90_bpm(void) {
    FollowBeatOut out = feed_click_train(10667, 16000 * 10);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - 90.0f) < 3.0f);
}

// 150 BPM: one beat every 0.4s = 6400 samples at 16kHz.
void test_detects_150_bpm(void) {
    FollowBeatOut out = feed_click_train(6400, 16000 * 10);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - 150.0f) < 3.0f);
}

// Regression test for the DC/mean-removal fix: a signal with a nonzero,
// *sustained* baseline loudness between beats (unlike the click trains
// above, which sit near zero/silent between clicks -- this is closer to
// continuous rhythmic material like sustained/held notes) plus a short
// louder pulse once per beat at a known BPM. Without mean-centering the
// autocorrelation, the nonzero baseline term dominates every lag equally and
// confidence collapses toward ~1.0 regardless of the pulse -- the detector
// would never report valid on this kind of "never silent between beats"
// real-world audio.
void test_detects_120_bpm_with_nonzero_baseline(void) {
    const float bpm         = 120.0f;
    const int   period      = 8000;    // one beat every 0.5s at 16kHz
    const int   pulse_len   = 300;     // short louder pulse at the top of each beat
    const int16_t baseline  = 6000;    // nonzero DC floor between pulses (never silent)
    const int16_t pulse_amp = 20000;
    const int   n_samples   = 16000 * 10;

    FollowBeatOut out = {0};
    for (int i = 0; i < n_samples; i++) {
        int16_t s = (i % period) < pulse_len ? pulse_amp : baseline;
        out = follow_beat_push_sample(&f, s);
    }
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_TRUE(fabsf(out.bpm - bpm) < 3.0f);
}

// Pure noise (no periodic structure) must never cross the confidence
// threshold -- "don't report garbage" is the whole point of `valid`.
void test_noise_never_valid(void) {
    FollowBeatOut out = {0};
    unsigned seed = 12345;
    for (int i = 0; i < 16000 * 10; i++) {
        seed = seed * 1103515245u + 12345u;
        int16_t s = (int16_t)((seed >> 16) & 0x3FF) - 512;  // small pseudo-random noise
        out = follow_beat_push_sample(&f, s);
    }
    TEST_ASSERT_FALSE(out.valid);
}

// reset() clears state -- a detector fed a strong 120 BPM signal, then reset,
// must not still report valid on the very next sample.
void test_reset_clears_state(void) {
    feed_click_train(8000, 16000 * 10);
    follow_beat_reset(&f);
    FollowBeatOut out = follow_beat_push_sample(&f, 0);
    TEST_ASSERT_FALSE(out.valid);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_not_valid_before_window_fills);
    RUN_TEST(test_detects_120_bpm);
    RUN_TEST(test_detects_90_bpm);
    RUN_TEST(test_detects_150_bpm);
    RUN_TEST(test_detects_120_bpm_with_nonzero_baseline);
    RUN_TEST(test_noise_never_valid);
    RUN_TEST(test_reset_clears_state);
    return UNITY_END();
}
