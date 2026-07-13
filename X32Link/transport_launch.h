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
    // ESP-025 REALIGN: re-anchor a RUNNING slave's pattern to the bar line without
    // stopping it. NOT a tempo fix -- Link owns tempo, so nothing is ever out of
    // "sync"; what drifts is the slave's pattern POSITION (it started on the wrong
    // beat, or free-ran and came back offset). Arms like Play and fires on the next
    // boundary. A no-op when stopped: press Play, that is what Play is for.
    TL_INTENT_REALIGN,
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
    // ESP-025: emit Stop (0xFC) then Start (0xFA) back-to-back, IN THAT ORDER.
    // 0xFA means "play from the top", so the slave's pattern restarts at step 1 --
    // landing on the bar line, which is the whole point. State stays TL_RUNNING:
    // the device never stopped, so the UI must not flicker through "stopped".
    TL_RESTART,
} TransportLaunchAction;

typedef struct {
    TransportLaunchState state;
    bool   have_last;     // is last_beats meaningful?
    double last_beats;    // previous tick's beat position (boundary-crossing test)
    bool   realign_armed; // ESP-025: realign pressed, waiting for the boundary
} TransportLaunch;

typedef struct {
    TransportLaunchAction action;
    TransportLaunchState  state;          // for the UI: stopped / armed / running
    // ESP-025: realign is armed while RUNNING, so it cannot be expressed as a state
    // without lying about whether the device is playing. The lit button blinks on
    // this; without it a quantized press looks like it did nothing for a whole bar
    // and the user presses again.
    bool                  realign_armed;
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
