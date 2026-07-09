// Pure phase-wrap-detection decision for the beat LED — see led_phase.h.
#include "led_phase.h"

bool led_phase_should_flash(float prev_phase, float phase, bool valid) {
    if (!valid) return false;
    if (prev_phase < 0.0f) return false;  // first reading since (re)sync: seed only
    return phase < prev_phase;            // wrapped: near-1 -> near-0
}

uint32_t led_flash_rgb(uint32_t beat_rgb, uint32_t accent_rgb, float bar_phase,
                       bool bar_valid, uint8_t bright) {
    uint32_t c = (bar_valid && (int)bar_phase == 0) ? accent_rgb : beat_rgb;
    uint32_t r = ((c >> 16) & 0xFF) * bright / 255;
    uint32_t g = ((c >>  8) & 0xFF) * bright / 255;
    uint32_t b = ( c        & 0xFF) * bright / 255;
    return (r << 16) | (g << 8) | b;
}
