#pragma once
// Pure WS2812 visual-metronome renderer (P4-018, customizable P4-019). Given the
// continuous beat position and a config (colors, brightness, pattern, fade), it
// fills an RGB array; the led_strip glue pushes it to the strip. Three patterns:
//   CHASE — one block per beat walks across the strip (npix/quantum px per beat)
//   FLASH — every pixel pulses together on each beat
//   FILL  — pixels fill across the bar like a progress bar, resetting each downbeat
// The bar-1 downbeat uses the accent colour, other beats the beat colour. No I/O.
// Host-tested in test/test_metro_strip.c. No Arduino/ESP-IDF dependency.
#include <stdint.h>

#define METRO_STRIP_PIXELS 8   // the attached strip length

// Pattern modes (config led_mode).
#define METRO_STRIP_CHASE 0
#define METRO_STRIP_FLASH 1
#define METRO_STRIP_FILL  2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t r, g, b; } RGB;

typedef struct {
    RGB     beat;    // colour for beats 2..N
    RGB     accent;  // colour for the bar-1 downbeat
    uint8_t bright;  // master brightness, 0..100 %
    uint8_t mode;    // METRO_STRIP_CHASE / FLASH / FILL
    uint8_t fade;    // dim across a beat, 0..100 % (0 = steady, 100 = dark by next beat)
} MetroStripCfg;

// Render the pattern for beat position `beats` into out[0..npix). `quantum` is
// beats per bar (e.g. 4). Safe for negative beats and npix not a multiple of
// quantum. Pixels not lit are set to {0,0,0}.
void metro_strip_render(double beats, int quantum, int npix,
                        const MetroStripCfg* cfg, RGB out[]);

// Standby breath (ESP-009): session joined, transport stopped. `phase` is a
// free-running 0..1 ramp (one full breath per period; the caller picks the
// rate). Every pixel pulses together in the beat colour, dim and never fully
// dark -- a strip that reaches zero looks like a board that crashed between
// blinks, which is the exact confusion this exists to prevent. Carries no beat
// information: in standby there is no beat.
void metro_strip_standby(double phase, int npix, const MetroStripCfg* cfg, RGB out[]);

#ifdef __cplusplus
}
#endif
