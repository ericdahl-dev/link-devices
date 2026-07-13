#include "buttons.h"

void button_reset(Button* b) {
    b->stable       = false;
    b->raw          = false;
    b->raw_since_ms = 0;
    b->primed       = false;
}

bool button_update(Button* b, bool raw_pressed, uint32_t now_ms) {
    // First call seeds the state. A button already held down at boot must NOT
    // read as a press edge — that would fire an action on power-up.
    if (!b->primed) {
        b->primed       = true;
        b->raw          = raw_pressed;
        b->stable       = raw_pressed;
        b->raw_since_ms = now_ms;
        return false;
    }

    if (raw_pressed != b->raw) {
        b->raw          = raw_pressed;
        b->raw_since_ms = now_ms;   // restart the settle window on every bounce
        return false;
    }

    // Level has been steady since raw_since_ms. Commit it once it outlasts a bounce.
    if (b->raw == b->stable) return false;
    if ((uint32_t)(now_ms - b->raw_since_ms) < BUTTON_DEBOUNCE_MS) return false;

    b->stable = b->raw;
    return b->stable;   // edge only on release -> press
}

bool button_is_pressed(const Button* b) {
    return b->stable;
}
