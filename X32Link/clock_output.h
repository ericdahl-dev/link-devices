#pragma once
// Pure per-output clock scheduler (P4-010): applies an output's swing (P4-013),
// phase nudge (signed milli-beats) and division (ppqn) to the shared beat
// position, then returns the pulses due via the tested clock_ticker. Swing is the
// non-linear time-warp (see swing.h); phase is a constant offset. Host-tested in
// test/test_clock_output.c. No Arduino/ESP-IDF dependency.
#include "clock_ticker.h"
#ifdef __cplusplus
extern "C" {
#endif

// Advance output `t` to beats_now and return pulses due at `ppqn` pulses/beat.
// The beat is swung by `swing_mbeats` (0 = straight; delays each off-eighth,
// P4-013) and then shifted by `phase_mbeats`/1000 of a beat (positive = fires
// earlier). Both are applied before quantizing.
int clock_output_due(ClockTicker* t, double beats_now, int ppqn,
                     int phase_mbeats, int swing_mbeats, int max_burst);

// --- ARC-019: the 1ms-writer derivation, one owner ---------------------------

// Burst cap for the 1ms writer tasks (Touch DIN, X32Link USB+DIN): a forward
// jump past this many pulses realigns (discarding the backlog into `dropped`)
// instead of spraying a catch-up burst — ESP-018 chose 4 on the bench; a burst
// is audible on downstream gear. KitchenSync's plan loop deliberately runs a
// different policy (KS_TICK_MAX_BURST 96: prefer catch-up over dropping) —
// a per-product choice, not a second copy of this constant.
#define CLOCK_OUTPUT_MAX_BURST 4

// Per-writer clock-out state: the tick grid plus the lifetime count of pulses
// discarded by realign/reset. The ticker's own `dropped` is zeroed by every
// re-prime (a pure struct can't tell first-init from re-prime), so the
// lifetime total is banked here, not in the ticker.
typedef struct {
    ClockTicker t;
    uint32_t    dropped;   // banked across resets; live total via _dropped()
} ClockOutput;

// Full clear: re-prime the grid AND zero the lifetime dropped count.
void clock_output_reset(ClockOutput* o);

// One 1ms-writer step. beats_now < 0 means phase is not valid (pre-sync, peer
// loss, mid re-measure): bank the ticker's dropped count, re-prime, emit
// nothing — the reset-on-invalid rule lives HERE, not in each writer task.
// Otherwise apply swing + phase nudge + division via clock_output_due() with
// the shared burst cap and return the pulses due now.
int clock_output_step(ClockOutput* o, double beats_now, int ppqn,
                      int phase_mbeats, int swing_mbeats);

// Lifetime pulses discarded (banked + whatever the live ticker holds). A stall
// long enough to trip the realign leaves no burst and no gap on the wire —
// this counter is the only trace (ESP-018).
uint32_t clock_output_dropped(const ClockOutput* o);

#ifdef __cplusplus
}
#endif
