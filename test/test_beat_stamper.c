// Host tests for the pure beat stamper (P4-032 Tier 3). Owns the one genuinely
// novel problem in Link Audio: assigning each mic block a beatsAtBufferBegin
// that is CONTINUOUS in beat time (Live reassembles the stream from these
// stamps -- a jittery stamp is an audible glitch), while still tracking the
// session (I2S crystal and Link timeline drift apart; a tempo change moves the
// map entirely). Policy: first block anchors to the measured session beat;
// after that each block advances by exactly frames/rate * beats-per-second,
// ignoring measured jitter, until the accumulated error passes a resync
// threshold -- then snap back to measured (one glitch beats permanent offset).
#include "unity.h"
#include "beat_stamper.h"
#include <math.h>

#define RATE 16000
#define ASSERT_BEATS(e, a) TEST_ASSERT_TRUE(fabs((a) - (e)) < 1e-9)

static BeatStamper bs;
void setUp(void)    { beat_stamper_reset(&bs); }
void tearDown(void) {}

// 120 BPM = 2 beats/sec. A 256-frame block at 16k = 16ms = 0.032 beats.
#define BPS_120 2.0
#define BLOCK   256
#define BLOCK_BEATS (BLOCK / (double)RATE * BPS_120)

// First block: no history -- anchor so the block ENDS at the measured beat
// (the measurement happens when the DMA read returns, i.e. at buffer end).
void test_first_block_anchors_to_measured_end(void) {
    double begin = beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    ASSERT_BEATS(10.0 - BLOCK_BEATS, begin);
}

// Steady stream: stamps advance by exactly the block length in beats,
// regardless of small jitter in the measured session beat.
void test_steady_stream_advances_exactly_one_block(void) {
    double b0 = beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    double b1 = beat_stamper_stamp(&bs, 10.0 + BLOCK_BEATS + 0.001, BPS_120, BLOCK, RATE);
    double b2 = beat_stamper_stamp(&bs, 10.0 + 2 * BLOCK_BEATS - 0.002, BPS_120, BLOCK, RATE);
    ASSERT_BEATS(b0 + BLOCK_BEATS, b1);
    ASSERT_BEATS(b1 + BLOCK_BEATS, b2);
}

// Measured drift beyond the threshold (session jumped: transport relocate,
// peer joined, tempo map replaced): resync to measured rather than carrying a
// stale offset forever.
void test_large_drift_resyncs_to_measured(void) {
    beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    // next measured end is a whole beat away from where continuity predicts
    double begin = beat_stamper_stamp(&bs, 12.0, BPS_120, BLOCK, RATE);
    ASSERT_BEATS(12.0 - BLOCK_BEATS, begin);
}

// Tempo change: beats-per-second doubles; the NEXT block must advance by the
// new block-length-in-beats (continuity in the new map, no resync needed while
// under threshold).
void test_tempo_change_advances_at_new_rate(void) {
    double b0 = beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    double new_bps = 4.0;   // 240 BPM
    double new_block_beats = BLOCK / (double)RATE * new_bps;
    double b1 = beat_stamper_stamp(&bs, b0 + BLOCK_BEATS + new_block_beats, new_bps, BLOCK, RATE);
    ASSERT_BEATS(b0 + BLOCK_BEATS, b1);   // begins where the last block ended
    (void)new_block_beats;
}

// Reset rearms anchoring.
void test_reset_reanchors(void) {
    beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    beat_stamper_reset(&bs);
    double begin = beat_stamper_stamp(&bs, 50.0, BPS_120, BLOCK, RATE);
    ASSERT_BEATS(50.0 - BLOCK_BEATS, begin);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_block_anchors_to_measured_end);
    RUN_TEST(test_steady_stream_advances_exactly_one_block);
    RUN_TEST(test_large_drift_resyncs_to_measured);
    RUN_TEST(test_tempo_change_advances_at_new_rate);
    RUN_TEST(test_reset_reanchors);
    return UNITY_END();
}
