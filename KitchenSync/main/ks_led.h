#pragma once
// KitchenSync glue: WS2812 strip over the RMT led_strip driver (P4-018). Thin I/O only —
// the pixel pattern is the pure, host-tested metro_strip.c. Renders the visual
// metronome on an external addressable strip wired to a GPIO (data), 5V, GND.
#include "metro_strip.h"   // RGB
#ifdef __cplusplus
extern "C" {
#endif

// Initialise the RMT WS2812 device on `gpio` for `npix` pixels. Safe to call once
// at boot even if the LED feature is off; a failure logs and disables show/clear.
void ks_led_start(int gpio, int npix);

// Push `npix` pixels to the strip (set + refresh). No-op if init failed.
void ks_led_show(const RGB* px, int npix);

// Turn every pixel off.
void ks_led_clear(void);

#ifdef __cplusplus
}
#endif
