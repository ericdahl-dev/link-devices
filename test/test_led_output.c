// Host tests for the pure led_output dispatcher (ESP-046, increment 1). The
// dispatcher is the single owner of brightness: it renders the METRONOME style
// via metro_strip_render at full scale, then applies cfg->bright once over the
// whole buffer. STATUS / EFFECT are stubs for later tickets. No I/O, no
// Arduino/ESP-IDF dependency.
#include "unity.h"
#include "led_output.h"
#include "metro_strip.h"
#include <stdbool.h>

#define NPIX METRO_STRIP_PIXELS

static LedCfg      cfg;
static LedSnapshot snap;

void setUp(void) {
    cfg.enable       = 1;
    cfg.style        = LED_STYLE_METRONOME;
    cfg.bright       = 100;
    cfg.beat_color   = (RGB){ 0,   180, 0   };   // green
    cfg.accent_color = (RGB){ 220, 110, 0   };   // amber
    cfg.mode         = METRO_STRIP_CHASE;
    cfg.fade         = 55;

    snap.beats       = 0.0;
    snap.quantum     = 4;
    snap.transport   = 1;
    snap.link_locked = 1;
}
void tearDown(void) {}

static bool lit(RGB c) { return c.r || c.g || c.b; }

// 1. METRONOME at bright=100 is a faithful passthrough of metro_strip_render.
void test_metronome_full_bright_matches_metro_strip(void) {
    RGB out[NPIX], ref[NPIX];
    MetroStripCfg m = { cfg.beat_color, cfg.accent_color, 100, cfg.mode, cfg.fade };
    snap.beats = 1.0;
    metro_strip_render(snap.beats, snap.quantum, NPIX, &m, ref);
    led_output_render(&snap, &cfg, NPIX, out);
    for (int i = 0; i < NPIX; i++) {
        TEST_ASSERT_EQUAL_UINT8(ref[i].r, out[i].r);
        TEST_ASSERT_EQUAL_UINT8(ref[i].g, out[i].g);
        TEST_ASSERT_EQUAL_UINT8(ref[i].b, out[i].b);
    }
}

// 2. bright=50 halves each lit channel (+- rounding) vs bright=100.
void test_bright_50_halves_channels(void) {
    RGB full[NPIX], half[NPIX];
    snap.beats = 1.0;
    cfg.bright = 100;
    led_output_render(&snap, &cfg, NPIX, full);
    cfg.bright = 50;
    led_output_render(&snap, &cfg, NPIX, half);
    for (int i = 0; i < NPIX; i++) {
        if (!lit(full[i])) continue;
        TEST_ASSERT_UINT8_WITHIN(1, full[i].r / 2, half[i].r);
        TEST_ASSERT_UINT8_WITHIN(1, full[i].g / 2, half[i].g);
        TEST_ASSERT_UINT8_WITHIN(1, full[i].b / 2, half[i].b);
    }
}

// 3. bright=0 blanks the strip.
void test_bright_0_is_dark(void) {
    RGB out[NPIX];
    snap.beats = 0.0;   // downbeat: metro_strip would light the accent block
    cfg.bright = 0;
    led_output_render(&snap, &cfg, NPIX, out);
    for (int i = 0; i < NPIX; i++) TEST_ASSERT_FALSE(lit(out[i]));
}

// 4. enable=0 blanks the strip regardless of style/brightness.
void test_disabled_is_dark(void) {
    RGB out[NPIX];
    snap.beats = 0.0;
    cfg.enable = 0;
    cfg.bright = 100;
    led_output_render(&snap, &cfg, NPIX, out);
    for (int i = 0; i < NPIX; i++) TEST_ASSERT_FALSE(lit(out[i]));
}

// 5. FLASH lights every pixel on the beat; off-beat it dims per fade. Verifies
// the dispatcher carries observable pattern behavior, not just the scale.
void test_flash_pattern_lights_and_fades(void) {
    RGB on[NPIX], off[NPIX];
    cfg.mode = METRO_STRIP_FLASH;
    cfg.fade = 80;
    snap.beats = 1.0;   // on the beat: brightest
    led_output_render(&snap, &cfg, NPIX, on);
    snap.beats = 1.9;   // late in the beat: fade has dimmed it
    led_output_render(&snap, &cfg, NPIX, off);
    for (int i = 0; i < NPIX; i++) {
        TEST_ASSERT_TRUE(lit(on[i]));           // FLASH lights the whole strip
        TEST_ASSERT_TRUE(on[i].g > off[i].g);   // off-beat dimmer per fade
        TEST_ASSERT_TRUE(off[i].g > 0);         // still lit, just dimmer
    }
}

// 6. A not-yet-implemented style (STATUS/EFFECT) blanks the strip — it must NOT
//    fall through to the metronome pattern. Guards the stub until ESP-048/050.
void test_unimplemented_style_is_dark(void) {
    RGB out[NPIX];
    snap.beats = 0.0;           // downbeat: metronome would light the accent block
    cfg.style  = LED_STYLE_STATUS;
    cfg.bright = 100;
    led_output_render(&snap, &cfg, NPIX, out);
    for (int i = 0; i < NPIX; i++) TEST_ASSERT_FALSE(lit(out[i]));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_metronome_full_bright_matches_metro_strip);
    RUN_TEST(test_bright_50_halves_channels);
    RUN_TEST(test_bright_0_is_dark);
    RUN_TEST(test_disabled_is_dark);
    RUN_TEST(test_flash_pattern_lights_and_fades);
    RUN_TEST(test_unimplemented_style_is_dark);
    return UNITY_END();
}
