// Host tests for the pure P4Hub runtime config (P4-007).
#include "unity.h"
#include "p4hub_config.h"
#include <string.h>

static P4HubConfig c;
void setUp(void)    { p4hub_config_defaults(&c); }
void tearDown(void) {}

void test_defaults(void) {
    TEST_ASSERT_EQUAL_INT(1, c.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(0, c.metronome_enable);   // audible feature: default off
    TEST_ASSERT_EQUAL_INT(1, c.metronome_accent);   // accent bar-1 when enabled
    TEST_ASSERT_EQUAL_STRING("", c.wifi_ssid);
    TEST_ASSERT_TRUE(p4hub_config_valid(&c));   // empty ssid is valid (AP mode)
}

// P4-010: output 0 is on by default (24-PPQN MIDI clock on cable 0), rest off.
void test_output_defaults(void) {
    TEST_ASSERT_EQUAL_INT(1,  c.clock[0].enable);
    TEST_ASSERT_EQUAL_INT(0,  c.clock[0].cable);
    TEST_ASSERT_EQUAL_INT(24, c.clock[0].ppqn);
    TEST_ASSERT_EQUAL_INT(0,  c.clock[0].phase_mbeats);
    TEST_ASSERT_EQUAL_INT(0,  c.clock[1].enable);
    TEST_ASSERT_EQUAL_INT(0,  c.clock[3].enable);
}

void test_set_ssid_and_pass(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "wifi_ssid", "TestNet"));
    TEST_ASSERT_EQUAL_STRING("TestNet", c.wifi_ssid);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "wifi_pass", "secret"));
    TEST_ASSERT_EQUAL_STRING("secret", c.wifi_pass);
}

// Blank password field keeps the current password (never wipes it).
void test_blank_pass_keeps_current(void) {
    p4hub_config_set(&c, "wifi_pass", "keepme");
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "wifi_pass", ""));
    TEST_ASSERT_EQUAL_STRING("keepme", c.wifi_pass);
}

// Per-output indexed form fields: clk<N>_en / _cable / _ppqn / _phase.
void test_output_set_and_ranges(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clk1_en", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.clock[1].enable);

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clk1_cable", "3"));
    TEST_ASSERT_EQUAL_INT(3, c.clock[1].cable);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk1_cable", "4"));   // 0..3
    TEST_ASSERT_EQUAL_INT(3, c.clock[1].cable);                   // unchanged

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clk0_ppqn", "12"));    // 1/8 division
    TEST_ASSERT_EQUAL_INT(12, c.clock[0].ppqn);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk0_ppqn", "0"));    // 1..48
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk0_ppqn", "49"));

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clk2_phase", "-50"));  // nudge earlier
    TEST_ASSERT_EQUAL_INT(-50, c.clock[2].phase_mbeats);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk2_phase", "300")); // ±250

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clk3_swing", "167"));  // triplet feel
    TEST_ASSERT_EQUAL_INT(167, c.clock[3].swing_mbeats);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk3_swing", "-1"));  // 0..250
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk3_swing", "251"));
    TEST_ASSERT_EQUAL_INT(167, c.clock[3].swing_mbeats);         // unchanged
}

// Out-of-range index and unknown per-output field are rejected.
void test_output_bad_index_and_field(void) {
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk4_en", "1"));      // only 0..3
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clk0_bogus", "1"));
}

void test_clock_out_toggle(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "clock_out", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.clock_out_enable);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "clock_out", "2"));   // only 0/1
}

void test_metronome_toggle(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "metronome", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.metronome_enable);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "metronome", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.metronome_enable);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "metronome", "2"));   // only 0/1
}

// The user-LED visual metronome has its own on/off switch (P4-018), independent
// of the audio metronome; default off.
void test_led_toggle(void) {
    TEST_ASSERT_EQUAL_INT(0, c.led_enable);                      // default off
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.led_enable);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.led_enable);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "led", "2"));         // only 0/1
}

// Strip customization (P4-019): brightness, mode, fade, and #rrggbb colours.
void test_led_customization(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led_bright", "40"));
    TEST_ASSERT_EQUAL_INT(40, c.led_brightness);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "led_bright", "101"));   // 0..100

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led_mode", "2"));        // fill
    TEST_ASSERT_EQUAL_INT(2, c.led_mode);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "led_mode", "3"));       // 0..2

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led_fade", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.led_fade);

    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led_beat", "#0080ff"));  // html color input
    TEST_ASSERT_EQUAL_INT(0x0080FF, c.led_beat_color);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "led_accent", "ff00aa")); // bare hex also ok
    TEST_ASSERT_EQUAL_INT(0xFF00AA, c.led_accent_color);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "led_beat", "#1000000")); // > 0xFFFFFF
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "led_beat", "#zzzzzz"));   // non-hex
}

void test_metronome_accent_toggle(void) {
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "metro_accent", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.metronome_accent);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "metro_accent", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.metronome_accent);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "metro_accent", "9")); // only 0/1
}

void test_unknown_key(void) {
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "bogus", "x"));
}

void test_metronome_volume_and_voice(void) {
    TEST_ASSERT_EQUAL_INT(80, c.metronome_volume);   // default
    TEST_ASSERT_EQUAL_INT(0,  c.metronome_voice);
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "metro_vol", "50"));
    TEST_ASSERT_EQUAL_INT(50, c.metronome_volume);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "metro_vol", "101"));  // 0..100
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "metro_vol", "-1"));
    TEST_ASSERT_TRUE(p4hub_config_set(&c, "metro_voice", "2"));   // Wood
    TEST_ASSERT_EQUAL_INT(2, c.metronome_voice);
    TEST_ASSERT_FALSE(p4hub_config_set(&c, "metro_voice", "3"));  // 0..2
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_output_defaults);
    RUN_TEST(test_set_ssid_and_pass);
    RUN_TEST(test_blank_pass_keeps_current);
    RUN_TEST(test_output_set_and_ranges);
    RUN_TEST(test_output_bad_index_and_field);
    RUN_TEST(test_clock_out_toggle);
    RUN_TEST(test_metronome_toggle);
    RUN_TEST(test_led_toggle);
    RUN_TEST(test_led_customization);
    RUN_TEST(test_metronome_accent_toggle);
    RUN_TEST(test_metronome_volume_and_voice);
    RUN_TEST(test_unknown_key);
    return UNITY_END();
}
