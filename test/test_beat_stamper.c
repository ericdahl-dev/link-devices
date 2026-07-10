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

// Steady jitter-free stream: stamps advance by exactly the block length in
// beats. (Under measurement jitter the servo intentionally lets stamps move
// by error/128 per block -- see the servo tests -- so exactness is only
// promised for exact input.)
void test_steady_stream_advances_exactly_one_block(void) {
    double b0 = beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    double b1 = beat_stamper_stamp(&bs, 10.0 + BLOCK_BEATS, BPS_120, BLOCK, RATE);
    double b2 = beat_stamper_stamp(&bs, 10.0 + 2 * BLOCK_BEATS, BPS_120, BLOCK, RATE);
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

// Servo: a persistent bias between measured and predicted (a bad first-block
// anchor -- one jittery boot-time measurement otherwise frozen in for the whole
// run) is pulled out over many blocks instead of lasting forever.
void test_servo_converges_out_a_biased_anchor(void) {
    // Anchor lands 20ms late at 120BPM: 0.04 beats of persistent bias.
    double bias = 0.04;
    beat_stamper_stamp(&bs, 10.0 + bias, BPS_120, BLOCK, RATE);
    // Feed the TRUE grid from here on; servo should walk the bias out.
    double measured = 10.0 + BLOCK_BEATS;
    double begin = 0;
    for (int k = 0; k < 2000; k++) {
        begin = beat_stamper_stamp(&bs, measured, BPS_120, BLOCK, RATE);
        measured += BLOCK_BEATS;
    }
    // begin of the last block should sit within 1ms-at-120BPM (0.002 beats)
    // of the true grid: measured(previous end) - BLOCK_BEATS.
    double truth = measured - BLOCK_BEATS - BLOCK_BEATS;
    TEST_ASSERT_TRUE(fabs(begin - truth) < 0.002);
}

// Servo must stay too slow to follow per-block jitter: alternating +/-j
// measurement noise moves consecutive stamps' spacing by (much) less than the
// noise itself.
void test_servo_rejects_symmetric_jitter(void) {
    beat_stamper_stamp(&bs, 10.0, BPS_120, BLOCK, RATE);
    double measured = 10.0;
    double prev = 10.0 - BLOCK_BEATS;
    double max_spacing_err = 0.0;
    for (int k = 1; k <= 200; k++) {
        measured += BLOCK_BEATS;
        double j = (k % 2 == 0) ? 0.01 : -0.01;   // +/-5ms at 120BPM
        double begin = beat_stamper_stamp(&bs, measured + j, BPS_120, BLOCK, RATE);
        double err = fabs((begin - prev) - BLOCK_BEATS);
        if (k > 1 && err > max_spacing_err) max_spacing_err = err;
        prev = begin;
    }
    TEST_ASSERT_TRUE(max_spacing_err < 0.0005);   // spacing wobble << jitter
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
    RUN_TEST(test_servo_converges_out_a_biased_anchor);
    RUN_TEST(test_servo_rejects_symmetric_jitter);
    RUN_TEST(test_reset_reanchors);
    return UNITY_END();
}
