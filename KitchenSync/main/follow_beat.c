#include "follow_beat.h"

#define FOLLOW_BEAT_DECIM (FOLLOW_BEAT_SAMPLE_RATE / FOLLOW_BEAT_ENV_RATE)  // 160

// One-pole lowpass alpha for a ~15Hz cutoff at 16kHz: alpha = dt/(RC+dt),
// RC = 1/(2*pi*fc). We only need the rhythm envelope, not audio fidelity.
#define ENV_LPF_ALPHA 0.006f

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

// Autocorrelation-based BPM search over the current ring contents. Only
// called once the ring is full (a complete FOLLOW_BEAT_ENV_WINDOW_S window).
// Lags are in envelope samples; bpm = 60 * FOLLOW_BEAT_ENV_RATE / lag.
static FollowBeatOut analyze(const FollowBeat* f) {
    // Unwrap the ring into a linear window -- it's small (400 floats), so a
    // copy is cheap next to the O(lags * window) correlation sum below, and
    // it keeps that loop free of modular indexing.
    float win[FOLLOW_BEAT_ENV_LEN];
    for (int i = 0; i < FOLLOW_BEAT_ENV_LEN; i++)
        win[i] = f->env_ring[(f->ring_pos + i) % FOLLOW_BEAT_ENV_LEN];

    int min_lag = (60 * FOLLOW_BEAT_ENV_RATE) / FOLLOW_BEAT_MAX_BPM;  // 200 BPM -> 30
    int max_lag = (60 * FOLLOW_BEAT_ENV_RATE) / FOLLOW_BEAT_MIN_BPM;  // 60 BPM -> 100

    float corr[FOLLOW_BEAT_ENV_LEN];  // indexed directly by lag (sparse use)
    float best_corr = -1.0f;
    float sum_corr  = 0.0f;
    int   n_lags    = 0;

    for (int lag = min_lag; lag <= max_lag; lag++) {
        float acc = 0.0f;
        int   n   = FOLLOW_BEAT_ENV_LEN - lag;
        for (int i = 0; i < n; i++) acc += win[i] * win[i + lag];
        acc /= (float)n;
        corr[lag] = acc;
        sum_corr += acc;
        n_lags++;
        if (acc > best_corr) best_corr = acc;
    }
    float mean_corr = n_lags > 0 ? sum_corr / (float)n_lags : 0.0f;

    // Octave-error guard: a periodic signal's autocorrelation is also strong
    // at integer multiples of the true period (half-tempo aliasing), so pick
    // the SHORTEST lag that's still nearly as strong as the global peak,
    // rather than the peak itself -- the true beat is the fastest strong one.
    int best_lag = min_lag;
    for (int lag = min_lag; lag <= max_lag; lag++) {
        if (corr[lag] >= 0.9f * best_corr) { best_lag = lag; break; }
    }

    float confidence = (mean_corr > 1e-9f) ? (best_corr / mean_corr) : 0.0f;

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
