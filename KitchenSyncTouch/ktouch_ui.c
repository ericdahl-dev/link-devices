// Pure transport-toggle logic — see ktouch_ui.h (ESP-016).
#include "ktouch_ui.h"

TransportLaunchIntent ktouch_toggle_intent(int launch_state) {
    // Stopped -> start (transport_launch quantizes it to the next bar). Armed
    // (waiting for the bar) or running -> stop: a second tap cancels an arm or
    // stops playback.
    return (launch_state == TL_STOPPED) ? TL_INTENT_PLAY : TL_INTENT_STOP;
}
