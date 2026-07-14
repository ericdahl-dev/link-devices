// Host tests for the pure KitchenSync Touch config (ESP-016): validate + set.
#include "unity.h"
#include "app_config.h"
#include "config.h"     // DEFAULT_* — assert defaults came back, not just "something"
#include <string.h>

static AppConfig c;
void setUp(void)    { config_defaults(&c); }
void tearDown(void) {}

// ---- ESP-030: MIGRATION -----------------------------------------------------
//
// Touch gains multiple WiFi slots (the P4 has had 3 since ESP-013; Touch's single
// slot is a deficiency, not a design choice).
//
// THE HAZARD these tests exist to prevent: config_decode DEFAULTS on a version
// mismatch -- it throws the persisted blob away. Bumping the version without
// migrating would make every Touch in the field LOSE ITS WIFI CREDENTIALS on the
// next boot, fall back to the KitchenSync-Setup SoftAP, and drop off the network.
// Annoying on a bench. A dead box at a venue.
//
// KitchenSync already hit this and solved it -- ks_config.c: "A v1 blob is
// MIGRATED, not discarded -- a version bump that silently forgot [wifi creds]".
// Same pattern here: FREEZE the v1 layout and read old bytes at OLD offsets.

// A byte-exact v1 blob, built through the FROZEN v1 layout -- never through the
// current struct, or this test would silently follow a layout change and prove
// nothing at all.
static void make_v1_blob(AppConfigV1* v1) {
    memset(v1, 0, sizeof(*v1));
    strcpy(v1->wifi_ssid, "Backline");
    strcpy(v1->wifi_pass, "hunter2");
    v1->quantum_beats    = 16;
    v1->clock_enable     = 1;
    v1->transport_enable = 1;
    v1->play_on_release  = 1;
    v1->nudge_mbeats     = -120;
    v1->brightness       = 55;
}

// THE ONE THAT MATTERS. A device in the field must keep its network.
void test_v1_blob_is_migrated_and_keeps_its_wifi_credentials(void) {
    AppConfigV1 v1;
    make_v1_blob(&v1);

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &v1, sizeof(v1), true, 1u);

    TEST_ASSERT_EQUAL_INT(CFG_DECODE_MIGRATED, r);
    TEST_ASSERT_EQUAL_STRING("Backline", out.wifi[0].ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2",  out.wifi[0].pass);
}

// The other v1 settings survive too. A migration that keeps the network but
// silently forgets the user's nudge is still a migration that lost their config.
void test_v1_migration_preserves_the_other_settings(void) {
    AppConfigV1 v1;
    make_v1_blob(&v1);

    AppConfig out;
    config_decode(&out, &v1, sizeof(v1), true, 1u);

    TEST_ASSERT_EQUAL_INT(16,   out.quantum_beats);
    TEST_ASSERT_EQUAL_INT(1,    out.clock_enable);
    TEST_ASSERT_EQUAL_INT(1,    out.transport_enable);
    TEST_ASSERT_EQUAL_INT(1,    out.play_on_release);
    TEST_ASSERT_EQUAL_INT(-120, out.nudge_mbeats);
    TEST_ASSERT_EQUAL_INT(55,   out.brightness);
}

// v1 had ONE credential, so the new slots come up EMPTY -- never filled with
// whatever bytes happened to follow a shorter blob.
void test_v1_migration_leaves_the_new_slots_empty(void) {
    AppConfigV1 v1;
    make_v1_blob(&v1);

    AppConfig out;
    config_decode(&out, &v1, sizeof(v1), true, 1u);

    for (int i = 1; i < KS_WIFI_SLOTS; i++) {
        TEST_ASSERT_EQUAL_STRING("", out.wifi[i].ssid);
        TEST_ASSERT_EQUAL_STRING("", out.wifi[i].pass);
    }
}

// A v1 blob of the WRONG SIZE is still garbage -- we can only migrate bytes we can
// read at KNOWN offsets. Fail closed, exactly as the unversioned path already does.
void test_a_v1_blob_of_the_wrong_size_still_defaults(void) {
    AppConfigV1 v1;
    make_v1_blob(&v1);

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &v1, sizeof(v1) - 4, true, 1u);

    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, r);
}

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
    strcpy(saved.wifi[0].ssid, "studio");
    strcpy(saved.wifi[0].pass, "hunter2");

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved),
                                        true, APP_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_OK, r);
    TEST_ASSERT_EQUAL_INT(-120, out.nudge_mbeats);
    TEST_ASSERT_EQUAL_INT(55,   out.brightness);
    TEST_ASSERT_EQUAL_INT(16,   out.quantum_beats);
    TEST_ASSERT_EQUAL_STRING("studio",  out.wifi[0].ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2", out.wifi[0].pass);
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
    // ESP-030 migration — the tests that stop a field device losing its network.
    RUN_TEST(test_v1_blob_is_migrated_and_keeps_its_wifi_credentials);
    RUN_TEST(test_v1_migration_preserves_the_other_settings);
    RUN_TEST(test_v1_migration_leaves_the_new_slots_empty);
    RUN_TEST(test_a_v1_blob_of_the_wrong_size_still_defaults);
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
