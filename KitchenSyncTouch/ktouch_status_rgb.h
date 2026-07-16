#pragma once
// ESP-043: the headless Super Mini's onboard WS2812 (GPIO48) as a status light. A screenless
// clock box has no other local feedback, so the colour must read at a glance across a room.
// This is the STATE -> APPEARANCE decision as a PURE function, host-tested -- it does not live
// in loop() (the same rule that keeps transport/tempo mapping out of the views).
//
//   stopped                -> dark
//   armed                  -> amber blink at beat rate (a cocked hammer: the tap registered,
//                             waiting for the bar line -- the DeviceDetail armed cadence)
//   running, free-run      -> green beat flash
//   running, Link-locked   -> cyan beat flash (phase-locked to a session)
//   bar-1 downbeat         -> a brighter accent of the running/armed colour
//
// The flash is HARD-EDGED (bright for the first slice of each beat, dark the rest), never a
// soft throb -- a throb reads as "loading", and a beat is a hammer-fall.
#include <stdbool.h>
#include <stdint.h>
#include "transport_launch.h"   // TransportLaunchState (pure, host-tested)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t r, g, b; } StatusRgb;

// Map the live state to the LED colour for THIS instant. `beats` is the writer's continuous
// beat counter (ktouch_midi_beats); `quantum` is beats per bar, for the downbeat accent.
// link_locked is true only when the clock is phase-locked to a Link session.
StatusRgb ktouch_status_rgb(TransportLaunchState transport, bool link_locked,
                            float beats, int quantum);

#ifdef __cplusplus
}
#endif
