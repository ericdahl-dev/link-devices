#pragma once
// Pure per-output clock scheduler (P4-010): applies an output's phase nudge
// (signed milli-beats) and division (ppqn) to the shared beat position, then
// returns the pulses due via the tested clock_ticker. The phase shift is the
// only new timing beyond clock_ticker; swing is a deferred follow-up. Host-tested
// in test/test_clock_output.c. No Arduino/ESP-IDF dependency.
#include "clock_ticker.h"
#ifdef __cplusplus
extern "C" {
#endif

// Advance output `t` to beats_now and return pulses due at `ppqn` pulses/beat,
// with the grid shifted by phase_mbeats/1000 of a beat (positive = fires earlier).
int clock_output_due(ClockTicker* t, double beats_now, int ppqn, int phase_mbeats, int max_burst);

#ifdef __cplusplus
}
#endif
