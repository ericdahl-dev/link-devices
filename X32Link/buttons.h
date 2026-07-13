#pragma once
// ESP-025: pure debounce for physical buttons on the bench rig (ESP32 DevKit +
// screw-terminal breakout). No Arduino, no GPIO — the caller reads the pin and
// passes the raw level in, so the whole thing is host-testable.
//
// Wiring assumed: switch to GND with INPUT_PULLUP, so the pin reads LOW when
// pressed. The caller converts that to `raw_pressed` (i.e. !digitalRead(pin)) —
// this module never sees a pin polarity.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A bounce settles well inside 25 ms on a normal tactile switch; anything longer
// starts eating fast double-taps.
#define BUTTON_DEBOUNCE_MS 25

typedef struct {
    bool     stable;          // debounced level: true = pressed
    bool     raw;             // last raw level seen
    uint32_t raw_since_ms;    // when `raw` last changed
    bool     primed;          // false until the first update seeds the state
} Button;

void button_reset(Button* b);

// Feed the raw pin level. Returns true exactly once per debounced press
// (release -> press edge); never on release, never twice for one press.
bool button_update(Button* b, bool raw_pressed, uint32_t now_ms);

// Debounced level, for callers that want hold rather than edge.
bool button_is_pressed(const Button* b);

#ifdef __cplusplus
}
#endif
