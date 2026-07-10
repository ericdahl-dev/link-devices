#pragma once
// Pure quantized-launch state machine (ESP-011). Owns "when does Play actually
// become MIDI Start?" -- press play mid-bar and the rig must not lurch in
// immediately; it ARMS, waits for the next quantum boundary, and fires on the
// grid, so gear started from the web UI lands in time with the Link session.
//
// Start is quantized, Stop is immediate. That asymmetry is deliberate: it is
// Ableton's own launch behaviour and what muscle memory expects.
//
// One instance per clock output (P4-010 gives each output its own division,
// phase and swing; ESP-011 gives each its own transport, so the drum machine
// can start this bar and the synth two bars later).
//
// The emitted action still goes through the existing `transport` edge module
// before hitting the wire -- this decides WHEN, that one guarantees exactly
// one 0xFA/0xFC per transition. Host-tested in test/test_transport_launch.c.
// No Arduino/ESP-IDF dependency.
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// What the user asked for this tick (a button press, not a level).
typedef enum {
    TL_INTENT_NONE = 0,
    TL_INTENT_PLAY,
    TL_INTENT_STOP,
} TransportLaunchIntent;

typedef enum {
    TL_STOPPED = 0,
    TL_ARMED,     // play pressed, waiting for the next quantum boundary
    TL_RUNNING,
} TransportLaunchState;

typedef enum {
    TL_NONE = 0,
    TL_START,     // emit MIDI Start (0xFA) now
    TL_STOP,      // emit MIDI Stop (0xFC) now
} TransportLaunchAction;

typedef struct {
    TransportLaunchState state;
    bool   have_last;     // is last_beats meaningful?
    double last_beats;    // previous tick's beat position (boundary-crossing test)
} TransportLaunch;

typedef struct {
    TransportLaunchAction action;
    TransportLaunchState  state;   // for the UI: stopped / armed / running
} TransportLaunchOut;

void transport_launch_reset(TransportLaunch* t);

// Advance one tick. `beats` is the continuous session beat position, `quantum`
// beats per bar, `have_beat` whether a beat grid exists at all. With no grid
// there is nothing to quantize to, so Play starts immediately rather than
// hanging armed forever waiting for a boundary that will never arrive.
TransportLaunchOut transport_launch_step(TransportLaunch* t, TransportLaunchIntent intent,
                                         double beats, double quantum, bool have_beat);

#ifdef __cplusplus
}
#endif
