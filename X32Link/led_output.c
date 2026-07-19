#include "led_output.h"

static uint8_t scale8(uint8_t v, double s) {
    double x = v * s;
    if (x < 0)   x = 0;
    if (x > 255) x = 255;
    return (uint8_t)(x + 0.5);
}

void led_output_render(const LedSnapshot* snap, const LedCfg* cfg, int npix, RGB out[]) {
    // Blank first: covers enable==0 and the STATUS/EFFECT stubs (ESP-048/050),
    // so an unimplemented style never silently renders the metronome pattern.
    for (int i = 0; i < npix; i++) { out[i].r = out[i].g = out[i].b = 0; }
    if (!cfg->enable) return;

    switch (cfg->style) {
    case LED_STYLE_METRONOME: {
        MetroStripCfg mcfg = {
            cfg->beat_color, cfg->accent_color, 100, (uint8_t)cfg->mode, (uint8_t)cfg->fade
        };
        metro_strip_render(snap->beats, snap->quantum, npix, &mcfg, out);
        double s = cfg->bright / 100.0;
        for (int i = 0; i < npix; i++) {
            out[i].r = scale8(out[i].r, s);
            out[i].g = scale8(out[i].g, s);
            out[i].b = scale8(out[i].b, s);
        }
        break;
    }
    case LED_STYLE_STATUS:   // ESP-048
    case LED_STYLE_EFFECT:   // ESP-050
    default:
        break;               // stub: stays blank until its ticket lands
    }
}
