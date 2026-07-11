#pragma once
// Shared, pure clock-tick scheduler for tempo fan-out (LNK-027 USB-MIDI clock
// out at 24 PPQN; LNK-028 analog Eurorack sync at a configurable PPQN). It
// quantizes a *monotonic beat position* (from the Link phase pipeline) to N
// pulses per beat and reports how many pulses are due since the last call —
// phase-locked (first pulse on phase-zero), re-priming on a large jump
// (re-origin / stall) instead of flooding. A companion BarReset tracker fires
// once per bar boundary, for the analog "reset" trigger. No Arduino/hardware
// dependency — host-tested in test/test_clock_ticker.c. Glue owns the timer/RMT
// and the actual edge emission.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t  last_tick;  // last emitted 1/ppqn-beat grid index
    bool     primed;     // false until the first beats_now aligns the grid
    // ESP-018: pulses the realign path DISCARDED. On a forward jump bigger than
    // max_burst the ticker realigns rather than flooding the wire with a catch-up
    // burst -- the right musical call, but `return 0` throws every pending pulse
    // away, and until now it did so with NO trace: no burst on the analyzer, no
    // counter, no log. A stall long enough to trip it was simply invisible.
    // reset() ZEROES this, so it is always initialised (callers declare the struct on
    // the stack and call reset on it). Keeping a LIFETIME total across resets is the
    // caller's job -- a pure struct cannot distinguish "first init" from "re-prime".
    uint32_t dropped;
} ClockTicker;

// Clear state: the next ticks_due() call re-primes the grid (emits nothing).
void clock_ticker_reset(ClockTicker* s);

// Advance to beats_now and return how many pulses to emit now, at `ppqn` pulses
// per beat:
//   - first call after a reset primes the grid and returns 0 (no backlog dump);
//   - a backward or same-slot move returns 0;
//   - a forward move of N pulses returns N, UNLESS N exceeds max_burst (a tempo
//     re-origin or a long stall), in which case it re-primes and returns 0 — the
//     caller keeps phase alignment rather than flooding the output.
// beats_now < 0 is treated as 0 (a "not valid yet" sentinel from the glue).
// ppqn <= 0 is a no-op that returns 0 without touching state.
int clock_ticker_ticks_due(ClockTicker* s, double beats_now, int ppqn, int max_burst);

typedef struct {
    int64_t last_bar;   // last observed bar index = floor(beats / quantum)
    bool    primed;
} BarReset;

void bar_reset_reset(BarReset* s);

// True exactly once when beats_now crosses forward into the next bar (bar =
// floor(beats / quantum)). First call after a reset primes and returns false. A
// same-bar read returns false. A backward move or a forward jump of more than one
// bar (a re-origin) re-primes and returns false — better to miss a reset than to
// fire a false downbeat. quantum <= 0 returns false.
bool bar_reset_due(BarReset* s, double beats_now, double quantum);

#ifdef __cplusplus
}
#endif
