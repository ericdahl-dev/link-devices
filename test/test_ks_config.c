// Host tests for the pure KitchenSync runtime config (P4-007).
#include "unity.h"
#include "ks_config.h"
#include <string.h>

static KsConfig c;
void setUp(void)    { ks_config_defaults(&c); }
void tearDown(void) {}

void test_defaults(void) {
    TEST_ASSERT_EQUAL_INT(1, c.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(0, c.metronome_enable);   // audible feature: default off
    TEST_ASSERT_EQUAL_INT(1, c.metronome_accent);   // accent bar-1 when enabled
    TEST_ASSERT_EQUAL_STRING("", c.wifi_ssid);
    TEST_ASSERT_TRUE(ks_config_valid(&c));   // empty ssid is valid (AP mode)
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
    TEST_ASSERT_TRUE(ks_config_set(&c, "wifi_ssid", "TestNet"));
    TEST_ASSERT_EQUAL_STRING("TestNet", c.wifi_ssid);
    TEST_ASSERT_TRUE(ks_config_set(&c, "wifi_pass", "secret"));
    TEST_ASSERT_EQUAL_STRING("secret", c.wifi_pass);
}

// Blank password field keeps the current password (never wipes it).
void test_blank_pass_keeps_current(void) {
    ks_config_set(&c, "wifi_pass", "keepme");
    TEST_ASSERT_TRUE(ks_config_set(&c, "wifi_pass", ""));
    TEST_ASSERT_EQUAL_STRING("keepme", c.wifi_pass);
}

// Per-output indexed form fields: clk<N>_en / _cable / _ppqn / _phase.
void test_output_set_and_ranges(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "clk1_en", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.clock[1].enable);

    TEST_ASSERT_TRUE(ks_config_set(&c, "clk1_cable", "3"));
    TEST_ASSERT_EQUAL_INT(3, c.clock[1].cable);
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk1_cable", "4"));   // 0..3
    TEST_ASSERT_EQUAL_INT(3, c.clock[1].cable);                   // unchanged

    TEST_ASSERT_TRUE(ks_config_set(&c, "clk0_ppqn", "12"));    // 1/8 division
    TEST_ASSERT_EQUAL_INT(12, c.clock[0].ppqn);
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk0_ppqn", "0"));    // 1..48
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk0_ppqn", "49"));

    TEST_ASSERT_TRUE(ks_config_set(&c, "clk2_phase", "-50"));  // nudge earlier
    TEST_ASSERT_EQUAL_INT(-50, c.clock[2].phase_mbeats);
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk2_phase", "300")); // ±250

    TEST_ASSERT_TRUE(ks_config_set(&c, "clk3_swing", "167"));  // triplet feel
    TEST_ASSERT_EQUAL_INT(167, c.clock[3].swing_mbeats);
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk3_swing", "-1"));  // 0..250
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk3_swing", "251"));
    TEST_ASSERT_EQUAL_INT(167, c.clock[3].swing_mbeats);         // unchanged
}

// Out-of-range index and unknown per-output field are rejected.
void test_output_bad_index_and_field(void) {
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk4_en", "1"));      // only 0..3
    TEST_ASSERT_FALSE(ks_config_set(&c, "clk0_bogus", "1"));
}

void test_clock_out_toggle(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "clock_out", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.clock_out_enable);
    TEST_ASSERT_FALSE(ks_config_set(&c, "clock_out", "2"));   // only 0/1
}

void test_metronome_toggle(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "metronome", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.metronome_enable);
    TEST_ASSERT_TRUE(ks_config_set(&c, "metronome", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.metronome_enable);
    TEST_ASSERT_FALSE(ks_config_set(&c, "metronome", "2"));   // only 0/1
}

// The user-LED visual metronome has its own on/off switch (P4-018), independent
// of the audio metronome; default off.
void test_led_toggle(void) {
    TEST_ASSERT_EQUAL_INT(0, c.led_enable);                      // default off
    TEST_ASSERT_TRUE(ks_config_set(&c, "led", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.led_enable);
    TEST_ASSERT_TRUE(ks_config_set(&c, "led", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.led_enable);
    TEST_ASSERT_FALSE(ks_config_set(&c, "led", "2"));         // only 0/1
}

// Strip customization (P4-019): brightness, mode, fade, and #rrggbb colours.
void test_led_customization(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "led_bright", "40"));
    TEST_ASSERT_EQUAL_INT(40, c.led_brightness);
    TEST_ASSERT_FALSE(ks_config_set(&c, "led_bright", "101"));   // 0..100

    TEST_ASSERT_TRUE(ks_config_set(&c, "led_mode", "2"));        // fill
    TEST_ASSERT_EQUAL_INT(2, c.led_mode);
    TEST_ASSERT_FALSE(ks_config_set(&c, "led_mode", "3"));       // 0..2

    TEST_ASSERT_TRUE(ks_config_set(&c, "led_fade", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.led_fade);

    TEST_ASSERT_TRUE(ks_config_set(&c, "led_beat", "#0080ff"));  // html color input
    TEST_ASSERT_EQUAL_INT(0x0080FF, c.led_beat_color);
    TEST_ASSERT_TRUE(ks_config_set(&c, "led_accent", "ff00aa")); // bare hex also ok
    TEST_ASSERT_EQUAL_INT(0xFF00AA, c.led_accent_color);
    TEST_ASSERT_FALSE(ks_config_set(&c, "led_beat", "#1000000")); // > 0xFFFFFF
    TEST_ASSERT_FALSE(ks_config_set(&c, "led_beat", "#zzzzzz"));   // non-hex
}

void test_metronome_accent_toggle(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "metro_accent", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.metronome_accent);
    TEST_ASSERT_TRUE(ks_config_set(&c, "metro_accent", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.metronome_accent);
    TEST_ASSERT_FALSE(ks_config_set(&c, "metro_accent", "9")); // only 0/1
}

void test_unknown_key(void) {
    TEST_ASSERT_FALSE(ks_config_set(&c, "bogus", "x"));
}

void test_follow_beat_default_off(void) {
    TEST_ASSERT_EQUAL_INT(0, c.follow_beat_enable);
}

void test_follow_beat_set(void) {
    TEST_ASSERT_TRUE(ks_config_set(&c, "follow_beat", "1"));
    TEST_ASSERT_EQUAL_INT(1, c.follow_beat_enable);
    TEST_ASSERT_TRUE(ks_config_set(&c, "follow_beat", "0"));
    TEST_ASSERT_EQUAL_INT(0, c.follow_beat_enable);
    TEST_ASSERT_FALSE(ks_config_set(&c, "follow_beat", "2"));  // 0/1 only
}

void test_metronome_volume_and_voice(void) {
    TEST_ASSERT_EQUAL_INT(80, c.metronome_volume);   // default
    TEST_ASSERT_EQUAL_INT(0,  c.metronome_voice);
    TEST_ASSERT_TRUE(ks_config_set(&c, "metro_vol", "50"));
    TEST_ASSERT_EQUAL_INT(50, c.metronome_volume);
    TEST_ASSERT_FALSE(ks_config_set(&c, "metro_vol", "101"));  // 0..100
    TEST_ASSERT_FALSE(ks_config_set(&c, "metro_vol", "-1"));
    TEST_ASSERT_TRUE(ks_config_set(&c, "metro_voice", "2"));   // Wood
    TEST_ASSERT_EQUAL_INT(2, c.metronome_voice);
    TEST_ASSERT_FALSE(ks_config_set(&c, "metro_voice", "3"));  // 0..2
}

// ARC-016: the live-safe copy carries the no-reboot fields but never WiFi creds.
void test_live_safe_copy_carries_live_fields_not_wifi(void) {
    KsConfig live; ks_config_defaults(&live);
    strcpy(live.wifi_ssid, "home");        // dst keeps its own creds
    KsConfig cand; ks_config_defaults(&cand);
    strcpy(cand.wifi_ssid, "SHOULD_NOT_COPY");
    cand.led_enable = 1; cand.led_brightness = 42; cand.metronome_voice = 2;
    cand.clock[1].enable = 1; cand.clock[1].cable = 3; cand.clock[1].ppqn = 24;

    ks_config_live_safe_copy(&live, &cand);

    TEST_ASSERT_EQUAL_STRING("home", live.wifi_ssid);   // creds untouched (reboot-only)
    TEST_ASSERT_EQUAL_INT(1, live.led_enable);          // live fields carried
    TEST_ASSERT_EQUAL_INT(42, live.led_brightness);
    TEST_ASSERT_EQUAL_INT(2, live.metronome_voice);
    TEST_ASSERT_EQUAL_INT(1, live.clock[1].enable);
    TEST_ASSERT_EQUAL_INT(3, live.clock[1].cable);
    TEST_ASSERT_EQUAL_INT(24, live.clock[1].ppqn);
}

// --- P4-014: persisted-blob decode guard ------------------------------------
// ks_config_decode owns the "is this persisted blob safe to load?" decision, so
// a struct-layout change falls back to clean defaults instead of loading bytes
// shifted into the wrong fields. Pure: the caller (ks_config_nvs.c) hands over
// whatever NVS held; every judgement happens here.

// A blob that a CURRENT-version firmware would have written: right version,
// right size, valid contents.
static void make_good_blob(KsConfig* blob) {
    ks_config_defaults(blob);
    blob->metronome_volume = 42;          // a marker we can assert survived
    strcpy(blob->wifi_ssid, "Studio");
}

void test_decode_accepts_current_version_blob(void) {
    KsConfig blob; make_good_blob(&blob);
    KsConfig out;
    ks_decode_result r = ks_config_decode(&out, &blob, sizeof(blob), true, KS_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(KS_DECODE_OK, r);
    TEST_ASSERT_EQUAL_INT(42, out.metronome_volume);       // blob won, not defaults
    TEST_ASSERT_EQUAL_STRING("Studio", out.wifi_ssid);
}

// The upgrade case this whole ticket exists for: a blob written before the
// version key existed. Its bytes may be ANY old layout -- never trust them.
void test_decode_legacy_unversioned_blob_falls_back_to_defaults(void) {
    KsConfig blob; make_good_blob(&blob);
    KsConfig out;
    ks_decode_result r = ks_config_decode(&out, &blob, sizeof(blob), false, 0);
    TEST_ASSERT_EQUAL_INT(KS_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(80, out.metronome_volume);       // default, not the blob's 42
    TEST_ASSERT_EQUAL_STRING("", out.wifi_ssid);
}

// A blob from a DIFFERENT schema version: same size is not enough -- fields may
// have been reordered/reinterpreted, which sizeof can never detect.
void test_decode_version_mismatch_falls_back_to_defaults(void) {
    KsConfig blob; make_good_blob(&blob);
    KsConfig out;
    ks_decode_result r = ks_config_decode(&out, &blob, sizeof(blob), true, KS_CONFIG_VERSION + 1);
    TEST_ASSERT_EQUAL_INT(KS_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(80, out.metronome_volume);
}

// Size mismatch (a grown or shrunk struct) is rejected even when the version
// happens to match -- belt to the version's braces.
void test_decode_size_mismatch_falls_back_to_defaults(void) {
    KsConfig blob; make_good_blob(&blob);
    KsConfig out;
    ks_decode_result r = ks_config_decode(&out, &blob, sizeof(blob) - 4, true, KS_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(KS_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(80, out.metronome_volume);
}

// No blob at all (fresh board / NVS_NOT_FOUND -> len 0): plain defaults, and
// that's not an error worth distinguishing from a rejected blob.
void test_decode_absent_blob_yields_defaults(void) {
    KsConfig out;
    ks_decode_result r = ks_config_decode(&out, NULL, 0, false, 0);
    TEST_ASSERT_EQUAL_INT(KS_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(80, out.metronome_volume);
    TEST_ASSERT_TRUE(ks_config_valid(&out));
}

// Right version, right size, but the contents are out of range (bit-rot, or a
// version bump that was forgotten). ks_config_valid is the last gate.
void test_decode_invalid_contents_falls_back_to_defaults(void) {
    KsConfig blob; make_good_blob(&blob);
    blob.metronome_volume = 999;          // out of 0..100
    KsConfig out;
    ks_decode_result r = ks_config_decode(&out, &blob, sizeof(blob), true, KS_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(KS_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(80, out.metronome_volume);
    TEST_ASSERT_TRUE(ks_config_valid(&out));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_live_safe_copy_carries_live_fields_not_wifi);
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
    RUN_TEST(test_follow_beat_default_off);
    RUN_TEST(test_follow_beat_set);
    RUN_TEST(test_decode_accepts_current_version_blob);
    RUN_TEST(test_decode_legacy_unversioned_blob_falls_back_to_defaults);
    RUN_TEST(test_decode_version_mismatch_falls_back_to_defaults);
    RUN_TEST(test_decode_size_mismatch_falls_back_to_defaults);
    RUN_TEST(test_decode_absent_blob_yields_defaults);
    RUN_TEST(test_decode_invalid_contents_falls_back_to_defaults);
    return UNITY_END();
}
