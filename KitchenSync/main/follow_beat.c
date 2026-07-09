#include "follow_beat.h"

#define FOLLOW_BEAT_DECIM (FOLLOW_BEAT_SAMPLE_RATE / FOLLOW_BEAT_ENV_RATE)  // 160

// One-pole lowpass alpha for a ~15Hz cutoff at 16kHz: alpha = dt/(RC+dt),
// RC = 1/(2*pi*fc). We only need the rhythm envelope, not audio fidelity.
#define ENV_LPF_ALPHA 0.006f

// Lag search range (in envelope samples) and the size of the sparse corr[]
// array that only covers that range -- kept as local #defines since callers
// never need lag values, only the resulting BPM.
#define FOLLOW_BEAT_MIN_LAG ((60 * FOLLOW_BEAT_ENV_RATE) / FOLLOW_BEAT_MAX_BPM)  // 200 BPM -> 30
#define FOLLOW_BEAT_MAX_LAG ((60 * FOLLOW_BEAT_ENV_RATE) / FOLLOW_BEAT_MIN_BPM)  // 60 BPM -> 100
#define FOLLOW_BEAT_LAG_COUNT (FOLLOW_BEAT_MAX_LAG - FOLLOW_BEAT_MIN_LAG + 1)

void follow_beat_reset(FollowBeat* f) {
    for (int i = 0; i < FOLLOW_BEAT_ENV_LEN; i++) f->env_ring[i] = 0.0f;
    f->ring_pos    = 0;
    f->ring_full   = false;
    f->lp_state    = 0.0f;
    f->decim_count = 0;
    f->decim_peak  = 0.0f;
    f->last_out    = (FollowBeatOut){ .bpm = 0.0f, .confidence = 0.0f, .valid = false };
}

static float rectify(int16_t s) {
    float v = (float)s / 32768.0f;
    return v < 0.0f ? -v : v;
}

// Read envelope sample `i` slots ahead of the ring's oldest entry, without
// materializing a linear copy of the ring. Keeps analyze()'s stack footprint
// small enough for the future capture task's tight budget.
static inline float ring_at(const FollowBeat* f, int i) {
    return f->env_ring[(f->ring_pos + i) % FOLLOW_BEAT_ENV_LEN];
}

// Autocorrelation-based BPM search over the current ring contents. Only
// called once the ring is full (a complete FOLLOW_BEAT_ENV_WINDOW_S window).
// Lags are in envelope samples; bpm = 60 * FOLLOW_BEAT_ENV_RATE / lag.
static FollowBeatOut analyze(const FollowBeat* f) {
    int min_lag = FOLLOW_BEAT_MIN_LAG;
    int max_lag = FOLLOW_BEAT_MAX_LAG;

    // Mean-center before correlating: real (non-click-train) audio has a
    // nonzero baseline loudness, which otherwise dominates every lag equally
    // and collapses confidence toward ~1.0 regardless of actual rhythm
    // strength. Read straight from the ring (no linear win[] copy) to keep
    // this frame small.
    float mean = 0.0f;
    for (int i = 0; i < FOLLOW_BEAT_ENV_LEN; i++) mean += ring_at(f, i);
    mean /= (float)FOLLOW_BEAT_ENV_LEN;

    // Window variance (the lag-0 autocorrelation of the mean-centered
    // window). Used below to normalize the best lag's correlation into a
    // confidence score. This is deliberately NOT the average of corr[]
    // across the searched lag range: once mean-centered, that average goes
    // slightly *negative* for sparse/click-like signals (most lag pairs
    // straddle exactly one impulse, contributing a small negative product),
    // which flips best_corr/mean_corr's sign and breaks the ratio -- variance
    // is always >= 0 and gives a numerically stable "peak vs. total energy"
    // baseline instead.
    float variance = 0.0f;
    for (int i = 0; i < FOLLOW_BEAT_ENV_LEN; i++) {
        float d = ring_at(f, i) - mean;
        variance += d * d;
    }
    variance /= (float)FOLLOW_BEAT_ENV_LEN;

    // Sparse corr[], sized to only the lags actually searched (indexed by
    // lag - min_lag) rather than the full envelope length.
    float corr[FOLLOW_BEAT_LAG_COUNT];
    float best_corr = -1e30f;  // mean-centered products can be negative

    for (int lag = min_lag; lag <= max_lag; lag++) {
        float acc = 0.0f;
        int   n   = FOLLOW_BEAT_ENV_LEN - lag;
        for (int i = 0; i < n; i++)
            acc += (ring_at(f, i) - mean) * (ring_at(f, i + lag) - mean);
        acc /= (float)n;
        corr[lag - min_lag] = acc;
        if (acc > best_corr) best_corr = acc;
    }

    // Octave-error guard: a periodic signal's autocorrelation is also strong
    // at integer multiples of the true period (half-tempo aliasing), so pick
    // the SHORTEST lag that's still nearly as strong as the global peak,
    // rather than the peak itself -- the true beat is the fastest strong one.
    int best_lag = min_lag;
    for (int lag = min_lag; lag <= max_lag; lag++) {
        if (corr[lag - min_lag] >= 0.9f * best_corr) { best_lag = lag; break; }
    }

    float confidence = (variance > 1e-9f) ? (best_corr / variance) : 0.0f;

    FollowBeatOut o;
    o.bpm        = (60.0f * FOLLOW_BEAT_ENV_RATE) / (float)best_lag;
    o.confidence = confidence;
    o.valid      = confidence >= FOLLOW_BEAT_CONFIDENCE_THRESHOLD;
    return o;
}

FollowBeatOut follow_beat_push_sample(FollowBeat* f, int16_t sample) {
    float rect = rectify(sample);
    f->lp_state += ENV_LPF_ALPHA * (rect - f->lp_state);
    if (f->lp_state > f->decim_peak) f->decim_peak = f->lp_state;

    f->decim_count++;
    if (f->decim_count < FOLLOW_BEAT_DECIM) return f->last_out;
    f->decim_count = 0;

    f->env_ring[f->ring_pos] = f->decim_peak;
    f->decim_peak = 0.0f;
    f->ring_pos = (f->ring_pos + 1) % FOLLOW_BEAT_ENV_LEN;
    if (f->ring_pos == 0) f->ring_full = true;

    f->last_out = f->ring_full
        ? analyze(f)
        : (FollowBeatOut){ .bpm = 0.0f, .confidence = 0.0f, .valid = false };
    return f->last_out;
}
