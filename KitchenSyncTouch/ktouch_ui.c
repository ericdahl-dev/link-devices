// Pure transport-toggle logic — see ktouch_ui.h (ESP-016).
#include "ktouch_ui.h"

TransportLaunchIntent ktouch_toggle_intent(int launch_state) {
    // Stopped -> start (transport_launch quantizes it to the next bar). Armed
    // (waiting for the bar) or running -> stop: a second tap cancels an arm or
    // stops playback.
    return (launch_state == TL_STOPPED) ? TL_INTENT_PLAY : TL_INTENT_STOP;
}

KtouchTouchOut ktouch_touch_step(int launch_state, bool touch_ok, bool touched,
                                 bool was_touched, bool cueing, bool play_on_release) {
    KtouchTouchOut o = { TL_INTENT_NONE, cueing };

    // Sensor fault: cancel any cue and send nothing. A failed read is NOT a release
    // (that would drop the beat on a dropped I2C transaction) and must not be
    // ignored (that stranded the CUE panel on screen forever).
    if (!touch_ok) { o.cueing = false; return o; }

    if (touched && !was_touched) {              // ---- press ----
        if (!play_on_release) {
            o.intent = ktouch_toggle_intent(launch_state);   // digital: fire now
        } else if (launch_state == TL_STOPPED) {
            o.cueing = true;                                 // turntable: cue in hand
        } else {
            o.intent = TL_INTENT_STOP;                       // running/armed: stop now
        }
    } else if (!touched && was_touched) {       // ---- release ----
        if (cueing) { o.cueing = false; o.intent = TL_INTENT_PLAY; }   // the drop
    }
    // held, or idle: nothing. A press must not repeat while the finger is down.
    return o;
}
