// Host tests for the pure customizable visual-metronome renderer (P4-018/019):
// three patterns (chase / flash / fill), configurable colours, brightness, fade.
#include "unity.h"
#include "metro_strip.h"
#include <stdbool.h>

static RGB px[METRO_STRIP_PIXELS];
static MetroStripCfg cfg;

void setUp(void) {
    cfg.beat   = (RGB){ 0,   180, 0   };   // green
    cfg.accent = (RGB){ 220, 110, 0   };   // amber
    cfg.bright = 100;
    cfg.mode   = METRO_STRIP_CHASE;
    cfg.fade   = 55;
}
void tearDown(void) {}

static bool lit(RGB c) { return c.r || c.g || c.b; }
static int  count(void) { int n=0; for (int i=0;i<METRO_STRIP_PIXELS;i++) n += lit(px[i]); return n; }

// --- CHASE ---------------------------------------------------------------

void test_chase_downbeat_first_block_accent(void) {
    metro_strip_render(0.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(px[0].r > 0 && px[1].r > 0);       // amber
    for (int i = 2; i < METRO_STRIP_PIXELS; i++) TEST_ASSERT_FALSE(lit(px[i]));
}

void test_chase_beat_two_block_beat_color(void) {
    metro_strip_render(1.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(px[2].g > 0 && px[3].g > 0);
    TEST_ASSERT_EQUAL_UINT8(0, px[2].r);                // beat colour is green (no red)
    TEST_ASSERT_EQUAL_INT(2, count());
}

void test_chase_wraps_each_bar(void) {
    metro_strip_render(4.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(px[0].r > 0);                      // downbeat block again
}

void test_chase_fade_dims_within_beat(void) {
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    uint8_t bright = px[4].g;
    metro_strip_render(2.9, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(bright > px[4].g && px[4].g > 0);
}

// fade=0 => steady (no dim across the beat).
void test_fade_zero_is_steady(void) {
    cfg.fade = 0;
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    uint8_t a = px[4].g;
    metro_strip_render(2.9, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_EQUAL_UINT8(a, px[4].g);
}

// --- FLASH ---------------------------------------------------------------

void test_flash_lights_all_pixels(void) {
    cfg.mode = METRO_STRIP_FLASH;
    metro_strip_render(1.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_EQUAL_INT(METRO_STRIP_PIXELS, count());
    TEST_ASSERT_EQUAL_UINT8(0, px[5].r);               // beat colour (green)
}

void test_flash_downbeat_is_accent(void) {
    cfg.mode = METRO_STRIP_FLASH;
    metro_strip_render(0.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_EQUAL_INT(METRO_STRIP_PIXELS, count());
    TEST_ASSERT_TRUE(px[7].r > 0);                     // amber everywhere
}

// --- FILL ----------------------------------------------------------------

// Halfway through the bar, half the strip is filled.
void test_fill_progress_half_bar(void) {
    cfg.mode = METRO_STRIP_FILL;
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, &cfg, px);   // progress = 2/4
    TEST_ASSERT_EQUAL_INT(4, count());
    TEST_ASSERT_TRUE(lit(px[0]) && lit(px[3]));
    TEST_ASSERT_FALSE(lit(px[4]));
}

// Fill grows across the bar.
void test_fill_grows(void) {
    cfg.mode = METRO_STRIP_FILL;
    metro_strip_render(1.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    int a = count();
    metro_strip_render(3.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(count() > a);
}

// --- BRIGHTNESS + COLOURS ------------------------------------------------

void test_brightness_scales_down(void) {
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, &cfg, px);   // bright 100
    uint8_t full = px[4].g;
    cfg.bright = 50;
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(px[4].g < full && px[4].g > 0);
    TEST_ASSERT_TRUE(px[4].g <= full / 2 + 2 && px[4].g >= full / 2 - 2);
}

void test_custom_colors_used(void) {
    cfg.beat = (RGB){ 0, 0, 200 };   // blue beats
    metro_strip_render(1.0, 4, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(px[2].b > 0 && px[2].g == 0);
}

/* ---- ESP-009: standby heartbeat ---------------------------------------- */
// Separate entry point from metro_strip_render: standby is not a beat pattern,
// it has no beat. Slow sinusoidal breath over the whole strip in the beat
// colour, dim, so "waiting for play" reads as alive-and-idle.

// Trough and peak of the breath differ, and neither is fully dark -- a strip
// that reaches zero looks like a board that crashed between blinks.
void test_standby_breathes_between_dim_and_brighter(void) {
    metro_strip_standby(0.0, METRO_STRIP_PIXELS, &cfg, px);   // trough
    int trough = px[0].g;
    metro_strip_standby(0.5, METRO_STRIP_PIXELS, &cfg, px);   // peak
    int peak = px[0].g;
    TEST_ASSERT_TRUE(trough > 0);
    TEST_ASSERT_TRUE(peak > trough);
}

// One period: phase 1.0 lands back at phase 0.0.
void test_standby_is_periodic(void) {
    metro_strip_standby(0.0, METRO_STRIP_PIXELS, &cfg, px);
    int at0 = px[0].g;
    metro_strip_standby(1.0, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_EQUAL_INT(at0, px[0].g);
}

// Every pixel breathes together -- no chase, no bar position. Standby carries
// no beat information because there is no beat.
void test_standby_lights_all_pixels_uniformly(void) {
    metro_strip_standby(0.5, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_EQUAL_INT(METRO_STRIP_PIXELS, count());
    for (int i = 1; i < METRO_STRIP_PIXELS; i++) {
        TEST_ASSERT_EQUAL_INT(px[0].r, px[i].r);
        TEST_ASSERT_EQUAL_INT(px[0].g, px[i].g);
        TEST_ASSERT_EQUAL_INT(px[0].b, px[i].b);
    }
}

// Standby is dimmer than the beat chase at its brightest -- it must never be
// mistaken for playback.
void test_standby_is_dimmer_than_a_beat(void) {
    metro_strip_standby(0.5, METRO_STRIP_PIXELS, &cfg, px);
    int standby_peak = px[0].g;
    metro_strip_render(0.0, 4, METRO_STRIP_PIXELS, &cfg, px);   // downbeat, accent lit
    int beat_peak = 0;
    for (int i = 0; i < METRO_STRIP_PIXELS; i++)
        if (px[i].r + px[i].g + px[i].b > beat_peak) beat_peak = px[i].r + px[i].g + px[i].b;
    TEST_ASSERT_TRUE(standby_peak < beat_peak);
}

// Master brightness still scales it.
void test_standby_respects_brightness(void) {
    metro_strip_standby(0.5, METRO_STRIP_PIXELS, &cfg, px);
    int full = px[0].g;
    cfg.bright = 25;
    metro_strip_standby(0.5, METRO_STRIP_PIXELS, &cfg, px);
    TEST_ASSERT_TRUE(px[0].g < full);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_standby_breathes_between_dim_and_brighter);
    RUN_TEST(test_standby_is_periodic);
    RUN_TEST(test_standby_lights_all_pixels_uniformly);
    RUN_TEST(test_standby_is_dimmer_than_a_beat);
    RUN_TEST(test_standby_respects_brightness);
    RUN_TEST(test_chase_downbeat_first_block_accent);
    RUN_TEST(test_chase_beat_two_block_beat_color);
    RUN_TEST(test_chase_wraps_each_bar);
    RUN_TEST(test_chase_fade_dims_within_beat);
    RUN_TEST(test_fade_zero_is_steady);
    RUN_TEST(test_flash_lights_all_pixels);
    RUN_TEST(test_flash_downbeat_is_accent);
    RUN_TEST(test_fill_progress_half_bar);
    RUN_TEST(test_fill_grows);
    RUN_TEST(test_brightness_scales_down);
    RUN_TEST(test_custom_colors_used);
    return UNITY_END();
}
