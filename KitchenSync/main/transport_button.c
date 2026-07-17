// ESP-045: single-button transport gesture — see transport_button.h. Pure.
#include "transport_button.h"

void transport_button_reset(TransportButton* tb) {
    button_reset(&tb->btn);
    tb->down       = false;
    tb->press_ms   = 0;
    tb->hold_fired = false;
}

ButtonGesture transport_button_update(TransportButton* tb, bool raw_pressed, uint32_t now_ms) {
    if (button_update(&tb->btn, raw_pressed, now_ms)) {   // debounced press edge
        tb->down       = true;
        tb->press_ms   = now_ms;
        tb->hold_fired = false;
        return BTN_GESTURE_NONE;
    }
    if (tb->down && !button_is_pressed(&tb->btn)) {       // released
        tb->down = false;
        return tb->hold_fired ? BTN_GESTURE_NONE : BTN_GESTURE_TAP;
    }
    // held past the threshold, still down: realign — fire once, and the later release
    // stays silent (hold_fired short-circuits the TAP above).
    if (tb->down && !tb->hold_fired && button_is_pressed(&tb->btn)
        && (now_ms - tb->press_ms) >= TRANSPORT_BUTTON_HOLD_MS) {
        tb->hold_fired = true;
        return BTN_GESTURE_HOLD;
    }
    return BTN_GESTURE_NONE;
}
