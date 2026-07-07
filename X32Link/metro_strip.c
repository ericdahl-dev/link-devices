#include "metro_strip.h"
#include <math.h>

// Colours are kept below full-scale on purpose: easier on the eyes and on the
// 5 V rail (8 pixels at full white is ~0.5 A).
static const RGB BEAT_COLOR   = { 0,   160, 0   };   // green — beats 2..N
static const RGB ACCENT_COLOR = { 200, 90,  0   };   // amber — bar-1 downbeat

// Fraction of brightness lost across a beat (bright at the beat, dimmer by the
// next). 0.55 -> fades from 100% down to 45%.
#define FADE 0.55

static uint8_t scale8(uint8_t v, double s) { return (uint8_t)(v * s + 0.5); }

void metro_strip_render(double beats, int quantum, int npix, RGB out[]) {
    for (int i = 0; i < npix; i++) { out[i].r = out[i].g = out[i].b = 0; }
    if (quantum < 1 || npix < quantum) return;

    int ppb = npix / quantum;            // pixels per beat (e.g. 8/4 = 2)
    if (ppb < 1) return;

    double n = floor(beats);
    int beat_in_bar = (((int)n % quantum) + quantum) % quantum;   // positive modulo
    double phase = beats - n;            // 0..1 within the beat
    double bright = 1.0 - FADE * phase;

    RGB c = (beat_in_bar == 0) ? ACCENT_COLOR : BEAT_COLOR;
    int start = beat_in_bar * ppb;
    for (int i = start; i < start + ppb && i < npix; i++) {
        out[i].r = scale8(c.r, bright);
        out[i].g = scale8(c.g, bright);
        out[i].b = scale8(c.b, bright);
    }
}
