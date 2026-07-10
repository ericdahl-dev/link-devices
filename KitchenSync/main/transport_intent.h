#pragma once
// One-shot transport intents from the web UI to the clock task (ESP-011).
// The HTTP handler posts a button press; the 1 ms clock loop takes it exactly
// once and hands it to ks_tick_step. A press must never be applied twice (two
// MIDI Starts) nor dropped, so take() clears as it reads.
#include "ks_config.h"
#include "transport_launch.h"
#ifdef __cplusplus
extern "C" {
#endif

// Post a press for output `out` (0..KS_CLOCK_OUTPUTS-1), or out < 0 for all.
void transport_intent_post(int out, TransportLaunchIntent intent);

// Move the pending intents into `dst` and clear them. Called once per tick.
void transport_intent_take(TransportLaunchIntent dst[KS_CLOCK_OUTPUTS]);

#ifdef __cplusplus
}
#endif
