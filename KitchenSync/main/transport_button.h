#pragma once
// ESP-045: one physical button, two gestures — the Mini's live transport surface.
// A short press TAPS (play/stop toggle); holding past a threshold is a HOLD (realign).
// Pure timing + debounce over buttons.h, so the whole gesture split is host-testable;
// the glue owns only the pin read and mapping the gesture to a TransportLaunchIntent
// (TAP -> play/stop per launch state, HOLD -> realign). No intent/launch-state
// dependency here on purpose — this module decides ONLY short-vs-long.
#include <stdbool.h>
#include <stdint.h>
#include "buttons.h"

#ifdef __cplusplus
extern "C" {
#endif

// Held this long (from the debounced press edge) turns a press into a HOLD. Short
// enough to feel deliberate, long enough that a normal tap never trips it.
#define TRANSPORT_BUTTON_HOLD_MS 600

typedef enum {
    BTN_GESTURE_NONE = 0,
    BTN_GESTURE_TAP,    // short press, reported on release -> play/stop toggle
    BTN_GESTURE_HOLD,   // held past the threshold, reported once while still down -> realign
} ButtonGesture;

typedef struct {
    Button   btn;         // debounce (edge + level)
    bool     down;        // a debounced press is in progress, not yet resolved
    uint32_t press_ms;    // when the debounced press edge landed
    bool     hold_fired;  // HOLD already emitted for this press (so release is silent)
} TransportButton;

void transport_button_reset(TransportButton* tb);

// Feed the raw pin level (true = pressed) and the current time. Returns a gesture at
// most once per press: HOLD the moment the press crosses TRANSPORT_BUTTON_HOLD_MS while
// still down; otherwise TAP when the press is released. NONE on every other tick.
ButtonGesture transport_button_update(TransportButton* tb, bool raw_pressed, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
