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

int main(void) {
    UNITY_BEGIN();
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
