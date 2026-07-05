#pragma once
// Pure free-running beat-position accumulator (P4-005). Integrates a possibly-
// changing tempo (micros_per_beat, from the Link timeline) over a monotonic
// microsecond clock into the continuous beat position clock_ticker consumes.
//
// Unlike link_phase_beats_now(), this is anchored to the LOCAL clock: it starts
// at beat 0 on the first advance, so it needs no session/ghost time alignment
// and never goes negative. It therefore conveys the correct *tempo* (clock rate)
// but not the session *phase* (downbeat) — right for a free-running 24-PPQN MIDI
// clock stream; phase-locked Start/downbeat would need the ghost-xform pipeline.
// Tempo changes are integrated at the current rate, so the position is smooth
// (no jump). Host-tested in test/test_beat_clock.c. No Arduino/ESP-IDF dep.
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double  beats;    // accumulated beat position
    int64_t last_us;  // monotonic time of the last advance
    bool    primed;   // false until the first advance anchors the clock
} BeatClock;

void beat_clock_reset(BeatClock* s);

// Advance to now_us at the current micros_per_beat and return the beat position.
// First call after a reset primes (returns 0, no jump). A non-forward now_us or
// micros_per_beat <= 0 returns the current position without advancing.
double beat_clock_advance(BeatClock* s, int64_t now_us, int64_t micros_per_beat);

#ifdef __cplusplus
}
#endif
