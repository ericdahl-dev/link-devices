#include "metro_strip.h"
#include <math.h>

static uint8_t scale8(uint8_t v, double s) {
    double x = v * s;
    if (x < 0)   x = 0;
    if (x > 255) x = 255;
    return (uint8_t)(x + 0.5);
}

static void set_px(RGB* p, RGB c, double s) {
    p->r = scale8(c.r, s);
    p->g = scale8(c.g, s);
    p->b = scale8(c.b, s);
}

void metro_strip_render(double beats, int quantum, int npix,
                        const MetroStripCfg* cfg, RGB out[]) {
    for (int i = 0; i < npix; i++) { out[i].r = out[i].g = out[i].b = 0; }
    if (!cfg || quantum < 1 || npix < 1) return;

    double n = floor(beats);
    int beat_in_bar = (((int)n % quantum) + quantum) % quantum;   // positive modulo
    double phase = beats - n;                                     // 0..1 within the beat

    RGB    color  = (beat_in_bar == 0) ? cfg->accent : cfg->beat;
    double master = cfg->bright / 100.0;
    double faded  = master * (1.0 - (cfg->fade / 100.0) * phase); // chase/flash pulse

    if (cfg->mode == METRO_STRIP_FLASH) {
        for (int i = 0; i < npix; i++) set_px(&out[i], color, faded);
        return;
    }
    if (cfg->mode == METRO_STRIP_FILL) {
        double progress = (beat_in_bar + phase) / (double)quantum; // 0..1 across the bar
        int litn = (int)(progress * npix + 0.5);
        if (litn > npix) litn = npix;
        for (int i = 0; i < litn; i++) set_px(&out[i], color, master); // steady fill
        return;
    }

    // CHASE (default): one block per beat walks across the strip.
    int ppb = npix / quantum;
    if (ppb < 1) ppb = 1;
    int start = beat_in_bar * ppb;
    for (int i = start; i < start + ppb && i < npix; i++) set_px(&out[i], color, faded);
}

// ESP-009. Sine breath between a dim floor and a modest ceiling -- deliberately
// below the beat chase's brightness so standby can never be mistaken for
// playback. Floor is non-zero: fully dark reads as "dead", which is the whole
// bug this fixes.
void metro_strip_standby(double phase, int npix, const MetroStripCfg* cfg, RGB out[]) {
    static const double kFloor = 0.06;   // never fully dark
    static const double kPeak  = 0.30;   // well under a lit beat pixel

    double ph = phase - floor(phase);                       // wrap to 0..1
    double breath = (1.0 - cos(2.0 * M_PI * ph)) * 0.5;     // 0 at ph=0, 1 at ph=0.5
    double level = (kFloor + (kPeak - kFloor) * breath) * (cfg->bright / 100.0);

    for (int i = 0; i < npix; i++) {
        out[i].r = scale8(cfg->beat.r, level);
        out[i].g = scale8(cfg->beat.g, level);
        out[i].b = scale8(cfg->beat.b, level);
    }
}
