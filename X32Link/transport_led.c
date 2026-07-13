// ESP-025 — see transport_led.h.
#include "transport_led.h"

static bool blink_phase(uint32_t now_ms) {
    return ((now_ms / TRANSPORT_LED_BLINK_MS) % 2) == 0;
}

bool transport_led_on(TransportLaunchState state, uint32_t now_ms) {
    switch (state) {
        case TL_RUNNING: return true;               // solid
        case TL_ARMED:   return blink_phase(now_ms);// "heard you, waiting for the bar line"
        case TL_STOPPED:
        default:         return false;
    }
}

bool realign_led_on(bool realign_armed, uint32_t now_ms) {
    return realign_armed && blink_phase(now_ms);
}
