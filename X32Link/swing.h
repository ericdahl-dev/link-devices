#pragma once
// Pure swing/shuffle time-warp (P4-013). Delays the off-eighth of each beat so a
// straight clock grooves. Within beat n, the eighth-note boundary that sits at
// n+0.5 in a straight feel is pushed to n + ratio, where
//   ratio = 0.5 + swing_mbeats/1000   (clamped to 0.75).
// swing_mbeats is milli-beats of delay applied to that boundary — same unit as the
// per-output phase nudge — so 0 = straight, ~167 = triplet (2:1) feel, 250 = max.
//
// The warp is continuous, monotonic, and maps every whole beat onto itself, so no
// pulses are gained or lost across a beat — they are just redistributed (long
// first eighth, short second). Feed the warped position to clock_ticker /
// clock_output before quantizing. Host-tested in test/test_swing.c. No
// Arduino/ESP-IDF dependency.
#ifdef __cplusplus
extern "C" {
#endif

// Map a linear beat position to its swung position for `swing_mbeats` of swing.
// swing_mbeats <= 0 returns beats unchanged (straight).
double swing_warp(double beats, int swing_mbeats);

#ifdef __cplusplus
}
#endif
