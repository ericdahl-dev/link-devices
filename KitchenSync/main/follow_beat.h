#pragma once
// Pure mic-based tempo detector (P4-020, "Follow Beat" v1: detect + display
// only). Owns envelope extraction (rectify + one-pole lowpass, decimated to a
// fixed analysis rate) and autocorrelation-based BPM estimation over a rolling
// window. No I2S/ESP-IDF dependency -- follow_beat_io.c feeds it raw mic
// samples at the native sample rate. Host-tested in test/test_follow_beat.c.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FOLLOW_BEAT_SAMPLE_RATE    16000  // native input rate (matches metronome_audio.c)
#define FOLLOW_BEAT_ENV_RATE       100    // decimated envelope analysis rate (Hz)
#define FOLLOW_BEAT_ENV_WINDOW_S   4      // rolling autocorrelation window, seconds
#define FOLLOW_BEAT_ENV_LEN        (FOLLOW_BEAT_ENV_RATE * FOLLOW_BEAT_ENV_WINDOW_S)  // 400
#define FOLLOW_BEAT_MIN_BPM        60
#define FOLLOW_BEAT_MAX_BPM        200
#define FOLLOW_BEAT_CONFIDENCE_THRESHOLD 2.5f  // peak/mean ratio needed to report valid

typedef struct {
    float bpm;
    float confidence;   // peak/mean ratio of the autocorrelation search range
    bool  valid;         // true once confidence clears FOLLOW_BEAT_CONFIDENCE_THRESHOLD
} FollowBeatOut;

typedef struct {
    float         env_ring[FOLLOW_BEAT_ENV_LEN];
    int           ring_pos;     // next write index (wraps)
    bool          ring_full;    // has the ring wrapped at least once?
    float         lp_state;     // one-pole lowpass state, rectified-input domain
    int           decim_count;  // samples accumulated toward the next envelope tick
    float         decim_peak;   // peak filtered value seen in the current decim block
    FollowBeatOut last_out;     // published between envelope ticks
} FollowBeat;

void follow_beat_reset(FollowBeat* f);

// Feed one raw mono sample at FOLLOW_BEAT_SAMPLE_RATE. Internally decimates to
// the envelope rate and recomputes the BPM estimate once per envelope tick,
// once the rolling window is full. Returns the latest estimate (unchanged
// between envelope ticks).
FollowBeatOut follow_beat_push_sample(FollowBeat* f, int16_t sample);

#ifdef __cplusplus
}
#endif
