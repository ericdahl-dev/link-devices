// Host tests for the pure WS2812 bar-position "chase" renderer (P4-018). Given a
// continuous beat position, it lights one block of pixels per beat that walks
// across the strip (2 pixels/beat at 4/4 on 8 pixels), amber on the bar-1
// downbeat and green elsewhere, dimming across each beat.
#include "unity.h"
#include "metro_strip.h"
#include <stdbool.h>

static RGB px[METRO_STRIP_PIXELS];

void setUp(void)    {}
void tearDown(void) {}

static bool lit(RGB c) { return c.r || c.g || c.b; }

// Downbeat (beat 0 of the bar): the first 2-pixel block is lit amber (r>0), the
// rest dark.
void test_downbeat_first_block_amber(void) {
    metro_strip_render(0.0, 4, METRO_STRIP_PIXELS, px);
    TEST_ASSERT_TRUE(px[0].r > 0 && px[1].r > 0);   // amber has a red component
    for (int i = 2; i < METRO_STRIP_PIXELS; i++) TEST_ASSERT_FALSE(lit(px[i]));
}

// Beat 2 lights the second block, green (no red), everything else dark.
void test_beat_two_second_block_green(void) {
    metro_strip_render(1.0, 4, METRO_STRIP_PIXELS, px);
    TEST_ASSERT_TRUE(px[2].g > 0 && px[3].g > 0);
    TEST_ASSERT_EQUAL_UINT8(0, px[2].r);            // green, not amber
    TEST_ASSERT_FALSE(lit(px[0]));
    TEST_ASSERT_FALSE(lit(px[4]));
}

// Only one block is ever lit — the block walks with the beat.
void test_third_beat_block_position(void) {
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, px);
    TEST_ASSERT_TRUE(lit(px[4]) && lit(px[5]));
    for (int i = 0; i < METRO_STRIP_PIXELS; i++)
        if (i != 4 && i != 5) TEST_ASSERT_FALSE(lit(px[i]));
}

// The chase wraps every bar: beat 4 (position 0 of the next bar) is the downbeat
// block again, amber.
void test_wraps_each_bar(void) {
    metro_strip_render(4.0, 4, METRO_STRIP_PIXELS, px);
    TEST_ASSERT_TRUE(px[0].r > 0 && px[1].r > 0);
    TEST_ASSERT_FALSE(lit(px[2]));
}

// A block dims across its beat (bright at the beat, fading toward the next).
void test_dims_within_beat(void) {
    metro_strip_render(2.0, 4, METRO_STRIP_PIXELS, px);
    uint8_t bright = px[4].g;
    metro_strip_render(2.9, 4, METRO_STRIP_PIXELS, px);
    uint8_t dim = px[4].g;
    TEST_ASSERT_TRUE(bright > dim);
    TEST_ASSERT_TRUE(dim > 0);                       // still lit, just dimmer
}

// Negative beat positions (a phase-locked session can read slightly < 0) don't
// index out of bounds and still land on a valid block.
void test_negative_beats_safe(void) {
    metro_strip_render(-0.5, 4, METRO_STRIP_PIXELS, px);
    int any = 0;
    for (int i = 0; i < METRO_STRIP_PIXELS; i++) any += lit(px[i]);
    TEST_ASSERT_TRUE(any == 2);                       // exactly one 2-pixel block
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_downbeat_first_block_amber);
    RUN_TEST(test_beat_two_second_block_green);
    RUN_TEST(test_third_beat_block_position);
    RUN_TEST(test_wraps_each_bar);
    RUN_TEST(test_dims_within_beat);
    RUN_TEST(test_negative_beats_safe);
    return UNITY_END();
}
