#pragma once
// Pure WS2812 "bar-position chase" renderer for the visual metronome (P4-018).
// Given the continuous beat position, it lights one contiguous block of pixels
// per beat that walks across the strip (npix/quantum pixels per beat), colouring
// the bar-1 downbeat differently (amber) from the other beats (green) and dimming
// the block across each beat so it pulses. No I/O — the caller renders into an RGB
// array and hands it to the led_strip glue. Host-tested in test/test_metro_strip.c.
// No Arduino/ESP-IDF dependency.
#include <stdint.h>

#define METRO_STRIP_PIXELS 8   // the attached strip length

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t r, g, b; } RGB;

// Render the chase for beat position `beats` into out[0..npix). `quantum` is beats
// per bar (e.g. 4). Pixels outside the current beat's block are set to {0,0,0}.
// Safe for negative beats and for npix not a multiple of quantum.
void metro_strip_render(double beats, int quantum, int npix, RGB out[]);

#ifdef __cplusplus
}
#endif
