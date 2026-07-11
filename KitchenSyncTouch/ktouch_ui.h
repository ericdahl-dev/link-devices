#pragma once
// Pure transport-toggle logic (ESP-016). Touch is transport-ONLY and the WHOLE
// screen is one toggle: no left/right buttons (so no mis-hit), no settings screen
// (so no stray-tap config change mid-performance). A tap sends the opposite of
// the current transport state. Host-tested in test/test_ktouch_ui.c.
#include "transport_launch.h"   // TransportLaunchIntent, TransportLaunchState

#ifdef __cplusplus
extern "C" {
#endif

// Given the current launch state (TL_STOPPED / TL_ARMED / TL_RUNNING), the intent
// a tap should post: stopped -> PLAY (arms, fires on the next bar); armed or
// running -> STOP (cancel the arm, or stop immediately).
TransportLaunchIntent ktouch_toggle_intent(int launch_state);

#ifdef __cplusplus
}
#endif
