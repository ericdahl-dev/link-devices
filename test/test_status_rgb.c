// ESP-043: the Super Mini's onboard status RGB. The state->colour mapping is a pure
// function so it can be asserted here rather than eyeballed on the glass -- a screenless
// box's only feedback has to be right without a person watching it.
#include "unity.h"
#include "ktouch_status_rgb.h"

void setUp(void) {}
void tearDown(void) {}

static bool is_dark(StatusRgb c) { return c.r == 0 && c.g == 0 && c.b == 0; }

// beats just past an integer => inside the hard-edged flash window; beats near .5 => between
// flashes (dark). quantum 4; whole beat 4 or 8 lands on a bar-1 downbeat (n % 4 == 0).
#define ON_BEAT   4.02f    // whole=4 (downbeat), inside flash
#define ON_BEAT2  5.02f    // whole=5 (beat 2 of the bar), inside flash
#define OFF_BEAT  5.50f    // between flashes

// Stopped is DARK regardless of the beat -- the light reflects transport, and a stopped box
// says nothing even while its clock free-runs.
void test_stopped_is_dark_even_on_the_beat(void) {
    TEST_ASSERT_TRUE(is_dark(ktouch_status_rgb(TL_STOPPED, false, ON_BEAT, 4)));
    TEST_ASSERT_TRUE(is_dark(ktouch_status_rgb(TL_STOPPED, true,  ON_BEAT, 4)));
}

// The flash is hard-edged: lit at the start of a beat, dark between beats.
void test_running_flashes_on_the_beat_and_is_dark_between(void) {
    TEST_ASSERT_FALSE(is_dark(ktouch_status_rgb(TL_RUNNING, false, ON_BEAT2, 4)));
    TEST_ASSERT_TRUE (is_dark(ktouch_status_rgb(TL_RUNNING, false, OFF_BEAT, 4)));
}

// Free-run is green, Link-locked is cyan -- you can tell across the room whether it caught
// the session. Green has no blue; cyan has blue.
void test_running_colour_tells_free_from_link(void) {
    StatusRgb freerun = ktouch_status_rgb(TL_RUNNING, false, ON_BEAT2, 4);
    StatusRgb locked  = ktouch_status_rgb(TL_RUNNING, true,  ON_BEAT2, 4);
    TEST_ASSERT_TRUE(freerun.g > 0 && freerun.b == 0);   // green
    TEST_ASSERT_TRUE(locked.b > 0  && locked.g > 0);     // cyan (green+blue)
}

// The bar-1 downbeat is a brighter accent of the same colour, so the "one" reads without a
// hue change that would fight the free/Link signal.
void test_downbeat_is_brighter_than_the_other_beats(void) {
    StatusRgb down = ktouch_status_rgb(TL_RUNNING, false, ON_BEAT,  4);   // whole 4 => beat 1
    StatusRgb beat = ktouch_status_rgb(TL_RUNNING, false, ON_BEAT2, 4);   // whole 5 => beat 2
    TEST_ASSERT_TRUE(down.g > beat.g);   // brighter green on the downbeat
}

// Armed blinks amber at the beat rate -- the cocked-hammer state, distinct from running.
void test_armed_blinks_amber_on_the_beat(void) {
    StatusRgb on  = ktouch_status_rgb(TL_ARMED, false, ON_BEAT2, 4);
    StatusRgb off = ktouch_status_rgb(TL_ARMED, false, OFF_BEAT, 4);
    TEST_ASSERT_TRUE(on.r > 0 && on.g > 0 && on.b == 0);   // amber = red+green, no blue
    TEST_ASSERT_TRUE(on.r > on.g);                          // redder than green => amber, not yellow
    TEST_ASSERT_TRUE(is_dark(off));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stopped_is_dark_even_on_the_beat);
    RUN_TEST(test_running_flashes_on_the_beat_and_is_dark_between);
    RUN_TEST(test_running_colour_tells_free_from_link);
    RUN_TEST(test_downbeat_is_brighter_than_the_other_beats);
    RUN_TEST(test_armed_blinks_amber_on_the_beat);
    return UNITY_END();
}
