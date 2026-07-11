#pragma once
// Pure transport-toggle logic (ESP-016). Touch is transport-ONLY and the WHOLE
// screen is one toggle: no left/right buttons (so no mis-hit), no settings screen
// (so no stray-tap config change mid-performance). A tap sends the opposite of
// the current transport state. Host-tested in test/test_ktouch_ui.c.
#include <stdbool.h>
#include "transport_launch.h"   // TransportLaunchIntent, TransportLaunchState

#ifdef __cplusplus
extern "C" {
#endif

// Given the current launch state (TL_STOPPED / TL_ARMED / TL_RUNNING), the intent
// a tap should post: stopped -> PLAY (arms, fires on the next bar); armed or
// running -> STOP (cancel the arm, or stop immediately).
TransportLaunchIntent ktouch_toggle_intent(int launch_state);

// What one touch sample decides. `cueing` is the NEW cue state -- true means the
// screen shows the CUE panel (held, armed in hand, no MIDI sent yet).
typedef struct {
    TransportLaunchIntent intent;
    bool                  cueing;
} KtouchTouchOut;

// The whole touch-edge decision, pure. The glue owns only the sensor read and the
// two carried bools (`was_touched`, `cueing`) -- this used to live inline in
// ktouch_display.cpp next to the Wire reads, where it could not be tested at all.
//
// Two trigger feels:
//   digital DJ   (play_on_release=0): a PRESS fires the toggle immediately.
//   turntable DJ (play_on_release=1): a press on a stopped deck CUES it -- armed in
//     hand, no MIDI yet -- and RELEASING drops it. transport_launch quantizes that
//     PLAY to the next "1", so releasing just before the bar lands the beat on the
//     downbeat. That release IS the feature.
//   Either feel: pressing a running/armed deck stops it immediately. Stop is urgent
//     and never waits for a release.
//
// `touch_ok` is false when the sensor read FAILED. A fault cancels a cue (cueing ->
// false, intent NONE). It must never be read as a release -- a dropped I2C
// transaction would otherwise fire the beat by itself. Nor can it be ignored: the
// old glue just skipped the tick, so a sensor that stopped answering stranded the
// CUE panel on screen forever. Debouncing a transient failure into a sustained one
// is the caller's job.
KtouchTouchOut ktouch_touch_step(int launch_state, bool touch_ok, bool touched,
                                 bool was_touched, bool cueing, bool play_on_release);

#ifdef __cplusplus
}
#endif
