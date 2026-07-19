#pragma once
// Pure RGB LED output dispatcher (ESP-046). Given a phase-accurate snapshot and
// a per-output config, it fills an RGB array for one strip. It is the single
// owner of brightness: the METRONOME style renders via metro_strip_render at
// full scale, then bright is applied once over the whole buffer. STATUS and
// EFFECT are stubs for later tickets (ESP-048/050). No I/O, no Arduino/ESP-IDF
// dependency. Host-tested in test/test_led_output.c.
#include "metro_strip.h"   // RGB, MetroStripCfg, metro_strip_render

#ifdef __cplusplus
extern "C" {
#endif

// Phase-accurate inputs. Increment 1 consumes only beats/quantum (METRONOME);
// transport/link_locked are carried for the STATUS style added later.
typedef struct {
    double beats;
    int    quantum;
    int    transport;
    int    link_locked;
} LedSnapshot;

enum { LED_STYLE_STATUS, LED_STYLE_METRONOME, LED_STYLE_EFFECT };

typedef struct {
    int enable;
    int style;
    int bright;              // master brightness, 0..100 %
    RGB beat_color, accent_color;
    int mode;                // METRO_STRIP_CHASE / FLASH / FILL
    int fade;
} LedCfg;

// Render `cfg`'s style for `snap` into out[0..npix). enable==0 blanks the strip.
void led_output_render(const LedSnapshot* snap, const LedCfg* cfg, int npix, RGB out[]);

#ifdef __cplusplus
}
#endif
