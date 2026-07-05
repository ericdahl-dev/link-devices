#pragma once
// Pure metronome scheduler (P4-006). From a monotonic beat position it decides
// WHEN to click and WHETHER the click is a bar-downbeat accent — nothing about
// sound. It composes the shared, host-tested engines UNCHANGED (ADR-0003):
//   - clock_ticker at ppqn=1  -> one pulse per whole beat (the click grid)
//   - BarReset at `quantum`   -> fires once per bar boundary (the accent)
// The two are driven off the SAME beats value each call, so they stay in
// lockstep (a bar boundary is always an integer beat). A large forward jump
// (tempo re-origin / stall) re-primes both instead of flooding, and a backward
// move is silent. The audio tone burst (ES8311/I2S) is the platform glue in
// P4Hub/main/metronome_audio.c. No Arduino/ESP-IDF dependency —
// host-tested in test/test_metronome.c.
#include "clock_ticker.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    METRO_NONE   = 0,   // no beat boundary crossed this update
    METRO_CLICK  = 1,   // plain beat click
    METRO_ACCENT = 2,   // bar-downbeat accent (bar 1 of `quantum`)
} MetroClick;

typedef struct {
    ClockTicker beat;   // ppqn=1: one tick per beat
    BarReset    bar;    // bar-boundary (downbeat) detector
} Metronome;

// Clear state: the next metronome_update() re-primes the grid (emits NONE).
void metronome_reset(Metronome* m);

// Advance to beats_now and report the click for this instant:
//   - the first call after a reset primes and returns METRO_NONE (no click);
//   - a whole-beat crossing returns METRO_ACCENT on a bar downbeat, else
//     METRO_CLICK;
//   - no crossing (mid-beat), a backward move, or a jump larger than max_burst
//     returns METRO_NONE (the jump re-primes so alignment is kept, not flooded).
// quantum = beats per bar (accent cadence, 1..N). max_burst caps beat catch-up.
MetroClick metronome_update(Metronome* m, double beats_now, double quantum, int max_burst);

#ifdef __cplusplus
}
#endif
