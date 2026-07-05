// Host tests for the pure metronome voice presets (P4-012).
#include "unity.h"
#include "metronome_voice.h"

void setUp(void)    {}
void tearDown(void) {}

static void get(int voice, float* ch, int* cm, float* ah, int* am) {
    metronome_voice_params(voice, ch, cm, ah, am);
}

// Each voice has a distinct, sane click+accent (accent higher than click).
void test_voices_distinct_and_sane(void) {
    float ch, ah; int cm, am;
    for (int v = 0; v < METRO_VOICE_COUNT; v++) {
        get(v, &ch, &cm, &ah, &am);
        TEST_ASSERT_TRUE(ch > 100.0f && ch < 8000.0f);   // audible pitch
        TEST_ASSERT_TRUE(cm > 5 && cm <= 64);            // fits the audio buffer
        TEST_ASSERT_TRUE(am > 5 && am <= 64);
        TEST_ASSERT_TRUE(ah > ch);                       // accent above the click
    }
}

// TONE is the documented default set.
void test_tone_defaults(void) {
    float ch, ah; int cm, am;
    get(METRO_VOICE_TONE, &ch, &cm, &ah, &am);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, ch);
    TEST_ASSERT_EQUAL_INT(45, cm);
    TEST_ASSERT_EQUAL_FLOAT(1760.0f, ah);
    TEST_ASSERT_EQUAL_INT(55, am);
}

// Distinct voices actually differ.
void test_click_differs_from_tone(void) {
    float t_ch, c_ch, ah; int cm, am;
    get(METRO_VOICE_TONE,  &t_ch, &cm, &ah, &am);
    get(METRO_VOICE_CLICK, &c_ch, &cm, &ah, &am);
    TEST_ASSERT_TRUE(c_ch != t_ch);
}

// Out-of-range voice falls back to TONE.
void test_out_of_range_is_tone(void) {
    float ch, ah, t_ch; int cm, am;
    get(METRO_VOICE_TONE, &t_ch, &cm, &ah, &am);
    get(99, &ch, &cm, &ah, &am);
    TEST_ASSERT_EQUAL_FLOAT(t_ch, ch);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_voices_distinct_and_sane);
    RUN_TEST(test_tone_defaults);
    RUN_TEST(test_click_differs_from_tone);
    RUN_TEST(test_out_of_range_is_tone);
    return UNITY_END();
}
