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

#ifdef __cplusplus
}
#endif
