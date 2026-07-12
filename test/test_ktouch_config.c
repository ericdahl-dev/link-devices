// Host tests for the pure KitchenSync Touch config (ESP-016): validate + set.
#include "unity.h"
#include "app_config.h"
#include "config.h"     // DEFAULT_* — assert defaults came back, not just "something"
#include <string.h>

static AppConfig c;
void setUp(void)    { config_defaults(&c); }
void tearDown(void) {}

void test_defaults_valid(void) {
    TEST_ASSERT_TRUE(config_validate(&c));
    TEST_ASSERT_EQUAL_INT(4, c.quantum_beats);
    TEST_ASSERT_EQUAL_INT(1, c.clock_enable);     // clock on by default
    TEST_ASSERT_EQUAL_INT(0, c.play_on_release);  // touch, not release
    TEST_ASSERT_EQUAL_INT(0, c.nudge_mbeats);     // no clock trim by default
}

// Tracer: a valid quantum is applied.
void test_set_quantum_in_range(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_QUANTUM_BEATS, 8));
    TEST_ASSERT_EQUAL_INT(8, c.quantum_beats);
}

// Out-of-range quantum is rejected, config unchanged. Range is Link beats: the web
// UI edits bars (x4), so 16 bars = 64 beats is the ceiling.
void test_set_quantum_out_of_range(void) {
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_QUANTUM_BEATS, 0));
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_QUANTUM_BEATS, 64));   // 16 bars ok
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_QUANTUM_BEATS, 65));  // past 16 bars
    TEST_ASSERT_EQUAL_INT(64, c.quantum_beats);
}

// Clock nudge: valid within +-250 millibeats, rejected past it.
void test_set_nudge(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_NUDGE_MBEATS, -120));
    TEST_ASSERT_EQUAL_INT(-120, c.nudge_mbeats);
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_NUDGE_MBEATS, 300));  // out of range
    TEST_ASSERT_EQUAL_INT(-120, c.nudge_mbeats);                   // unchanged
}

// Brightness: 10..100 percent; never full-dark (0) or over 100.
void test_set_brightness(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_BRIGHTNESS, 50));
    TEST_ASSERT_EQUAL_INT(50, c.brightness);
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_BRIGHTNESS, 0));    // would be dark
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_BRIGHTNESS, 120));  // over range
    TEST_ASSERT_EQUAL_INT(50, c.brightness);                    // unchanged
}

// Booleans only accept 0/1.
void test_set_bool_fields(void) {
    TEST_ASSERT_TRUE(app_config_set(&c, ACF_PLAY_ON_RELEASE, 1));
    TEST_ASSERT_EQUAL_INT(1, c.play_on_release);
    TEST_ASSERT_FALSE(app_config_set(&c, ACF_CLOCK_ENABLE, 2));   // not a bool
    TEST_ASSERT_EQUAL_INT(1, c.clock_enable);
}

// ---------------------------------------------------------------- ARC-020
// config_decode: the one owner of "is this persisted blob safe to load?".
// Mirrors test_ks_config.c. Every gate is fail-closed -> defaults.

// A blob written by this build decodes back verbatim.
void test_decode_round_trip(void) {
    AppConfig saved;
    config_defaults(&saved);
    TEST_ASSERT_TRUE(app_config_set(&saved, ACF_NUDGE_MBEATS, -120));
    TEST_ASSERT_TRUE(app_config_set(&saved, ACF_BRIGHTNESS, 55));
    TEST_ASSERT_TRUE(app_config_set(&saved, ACF_QUANTUM_BEATS, 16));
    strcpy(saved.wifi_ssid, "studio");
    strcpy(saved.wifi_pass, "hunter2");

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved),
                                        true, APP_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_OK, r);
    TEST_ASSERT_EQUAL_INT(-120, out.nudge_mbeats);
    TEST_ASSERT_EQUAL_INT(55,   out.brightness);
    TEST_ASSERT_EQUAL_INT(16,   out.quantum_beats);
    TEST_ASSERT_EQUAL_STRING("studio",  out.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2", out.wifi_pass);
}

// No blob at all (fresh flash) -> defaults.
void test_decode_absent_blob_defaults(void) {
    AppConfig out;
    cfg_decode_result r = config_decode(&out, NULL, 0, false, 0);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, r);
    TEST_ASSERT_TRUE(config_validate(&out));
    TEST_ASSERT_EQUAL_INT(4, out.quantum_beats);   // == defaults
}

// Bytes present but no version key (predates versioning) -> not vouched for.
void test_decode_no_version_defaults(void) {
    AppConfig saved;
    config_defaults(&saved);
    saved.brightness = 55;

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved), false, 0);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(DEFAULT_BRIGHTNESS, out.brightness);   // NOT 55
}

// A version we don't know -> defaults (never guess a layout).
void test_decode_wrong_version_defaults(void) {
    AppConfig saved;
    config_defaults(&saved);
    saved.brightness = 55;

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved),
                                        true, APP_CONFIG_VERSION + 99u);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(DEFAULT_BRIGHTNESS, out.brightness);
}

// Right version, wrong size -> the struct moved under us. Defaults.
void test_decode_size_mismatch_defaults(void) {
    AppConfig saved;
    config_defaults(&saved);
    saved.brightness = 55;

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved) - 4,
                                        true, APP_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, r);
    TEST_ASSERT_EQUAL_INT(DEFAULT_BRIGHTNESS, out.brightness);
}

// Version and size both vouch for it, but a field is out of range (bit-rot, or a
// layout shipped without a version bump). The guard still runs. Defaults.
void test_decode_invalid_contents_defaults(void) {
    AppConfig saved;
    config_defaults(&saved);
    saved.brightness = 0;        // below the 10% floor -> config_validate says no

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved),
                                        true, APP_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, r);
    TEST_ASSERT_TRUE(config_validate(&out));
    TEST_ASSERT_EQUAL_INT(DEFAULT_BRIGHTNESS, out.brightness);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_valid);
    RUN_TEST(test_set_quantum_in_range);
    RUN_TEST(test_set_quantum_out_of_range);
    RUN_TEST(test_set_nudge);
    RUN_TEST(test_set_brightness);
    RUN_TEST(test_set_bool_fields);
    RUN_TEST(test_decode_round_trip);
    RUN_TEST(test_decode_absent_blob_defaults);
    RUN_TEST(test_decode_no_version_defaults);
    RUN_TEST(test_decode_wrong_version_defaults);
    RUN_TEST(test_decode_size_mismatch_defaults);
    RUN_TEST(test_decode_invalid_contents_defaults);
    return UNITY_END();
}
