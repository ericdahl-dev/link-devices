// ESP-042: the final AppConfig->KsConfig migration. The Touch converges onto the shared
// KsConfig; a device already in the field holds a legacy Touch blob (KTouchLegacyV1..V4).
// This asserts that upgrading PRESERVES WiFi + every setting rather than dropping the box
// to the SoftAP portal — the exact failure the freeze-and-migrate discipline exists to
// prevent (ESP-013/030/037 all learned it).
#include "unity.h"
#include "ktouch_config_migrate.h"
#include "ktouch_legacy_config.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

// A fully-populated v4 blob (the LAST AppConfig) migrates field-for-field into KsConfig:
// WiFi across all slots, the single DIN output onto clock[0], tempo and the UI fields.
void test_v4_blob_migrates_every_field_into_ksconfig(void) {
    KTouchLegacyV4 v4;
    memset(&v4, 0, sizeof(v4));
    strcpy(v4.wifi[0].ssid, "StudioNet");  strcpy(v4.wifi[0].pass, "downbeat99");
    strcpy(v4.wifi[1].ssid, "HomeNet");    strcpy(v4.wifi[1].pass, "livingroom");
    v4.quantum_beats    = 8;      // 2 bars
    v4.clock_enable     = 1;
    v4.transport_enable = 0;
    v4.play_on_release  = 1;
    v4.nudge_mbeats     = -40;
    v4.brightness       = 65;
    v4.ppqn             = 48;
    v4.swing_mbeats     = 30;
    v4.tempo_mbpm       = 128500;

    KsConfig out;
    bool ok = ktouch_migrate_legacy_config(&out, &v4, sizeof(v4), 4u);
    TEST_ASSERT_TRUE(ok);

    // WiFi survives — the whole point.
    TEST_ASSERT_EQUAL_STRING("StudioNet", out.wifi[0].ssid);
    TEST_ASSERT_EQUAL_STRING("downbeat99", out.wifi[0].pass);
    TEST_ASSERT_EQUAL_STRING("HomeNet", out.wifi[1].ssid);
    TEST_ASSERT_EQUAL_STRING("livingroom", out.wifi[1].pass);

    TEST_ASSERT_EQUAL_INT(8, out.quantum_beats);
    TEST_ASSERT_EQUAL_INT(1, out.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(0, out.transport_enable);
    TEST_ASSERT_EQUAL_INT(1, out.play_on_release);
    TEST_ASSERT_EQUAL_INT(65, out.lcd_brightness);
    TEST_ASSERT_EQUAL_INT(128500, out.tempo_mbpm);

    // The Touch's single DIN output maps onto clock[0].
    TEST_ASSERT_EQUAL_INT(1, out.clock[0].enable);
    TEST_ASSERT_EQUAL_INT(48, out.clock[0].ppqn);
    TEST_ASSERT_EQUAL_INT(-40, out.clock[0].phase_mbeats);
    TEST_ASSERT_EQUAL_INT(30, out.clock[0].swing_mbeats);

    // The migrated blob is a valid KsConfig, same gate a verbatim load runs through.
    TEST_ASSERT_TRUE(ks_config_valid(&out));
}

// A v3 blob predates the settable tempo: it must migrate every field it HAS and DEFAULT
// the one it lacks (tempo 120.000 BPM) — a v3 box free-runs at 120 exactly as before.
void test_v3_blob_defaults_the_tempo_the_layout_lacked(void) {
    KTouchLegacyV3 v3;
    memset(&v3, 0, sizeof(v3));
    strcpy(v3.wifi[0].ssid, "Net3");  strcpy(v3.wifi[0].pass, "pass3");
    v3.quantum_beats = 4; v3.clock_enable = 1; v3.transport_enable = 1;
    v3.play_on_release = 0; v3.nudge_mbeats = 10; v3.brightness = 50;
    v3.ppqn = 12; v3.swing_mbeats = 60;

    KsConfig out;
    TEST_ASSERT_TRUE(ktouch_migrate_legacy_config(&out, &v3, sizeof(v3), 3u));
    TEST_ASSERT_EQUAL_STRING("Net3", out.wifi[0].ssid);
    TEST_ASSERT_EQUAL_INT(12, out.clock[0].ppqn);
    TEST_ASSERT_EQUAL_INT(60, out.clock[0].swing_mbeats);
    TEST_ASSERT_EQUAL_INT(120000, out.tempo_mbpm);   // the default it never carried
}

// A v2 blob predates ppqn+swing AND tempo: those three DEFAULT (24 / 0 / 120000), which
// is exactly what the writer hardcoded then, so a migrated box's clock is unchanged.
void test_v2_blob_defaults_ppqn_swing_and_tempo(void) {
    KTouchLegacyV2 v2;
    memset(&v2, 0, sizeof(v2));
    strcpy(v2.wifi[0].ssid, "Net2");  strcpy(v2.wifi[0].pass, "pass2");
    v2.quantum_beats = 16; v2.clock_enable = 0; v2.transport_enable = 1;
    v2.play_on_release = 1; v2.nudge_mbeats = -5; v2.brightness = 90;

    KsConfig out;
    TEST_ASSERT_TRUE(ktouch_migrate_legacy_config(&out, &v2, sizeof(v2), 2u));
    TEST_ASSERT_EQUAL_STRING("Net2", out.wifi[0].ssid);
    TEST_ASSERT_EQUAL_INT(16, out.quantum_beats);
    TEST_ASSERT_EQUAL_INT(0, out.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(24, out.clock[0].ppqn);
    TEST_ASSERT_EQUAL_INT(0, out.clock[0].swing_mbeats);
    TEST_ASSERT_EQUAL_INT(120000, out.tempo_mbpm);
}

// The v1 single credential becomes slot 0; the slots v1 never had come up EMPTY, never
// filled with whatever bytes followed the shorter blob.
void test_v1_blob_puts_the_one_credential_in_slot_zero(void) {
    KTouchLegacyV1 v1;
    memset(&v1, 0, sizeof(v1));
    strcpy(v1.wifi_ssid, "OldNet");  strcpy(v1.wifi_pass, "oldpass");
    v1.quantum_beats = 4; v1.clock_enable = 1; v1.transport_enable = 1;
    v1.play_on_release = 0; v1.nudge_mbeats = 0; v1.brightness = 80;

    KsConfig out;
    TEST_ASSERT_TRUE(ktouch_migrate_legacy_config(&out, &v1, sizeof(v1), 1u));
    TEST_ASSERT_EQUAL_STRING("OldNet", out.wifi[0].ssid);
    TEST_ASSERT_EQUAL_STRING("oldpass", out.wifi[0].pass);
    TEST_ASSERT_EQUAL_STRING("", out.wifi[1].ssid);   // empty, not garbage
    TEST_ASSERT_EQUAL_STRING("", out.wifi[2].ssid);
    TEST_ASSERT_EQUAL_INT(24, out.clock[0].ppqn);
    TEST_ASSERT_EQUAL_INT(120000, out.tempo_mbpm);
}

// A blob whose length doesn't match the claimed version's frozen layout is rejected —
// never read at the wrong offsets — and `out` is left as clean defaults.
void test_a_wrong_size_blob_for_its_version_is_rejected(void) {
    KTouchLegacyV4 v4;
    memset(&v4, 0, sizeof(v4));
    KsConfig out;
    TEST_ASSERT_FALSE(ktouch_migrate_legacy_config(&out, &v4, sizeof(v4) - 4, 4u));
    TEST_ASSERT_TRUE(ks_config_valid(&out));   // defaults, loadable
    TEST_ASSERT_EQUAL_INT(120000, out.tempo_mbpm);
}

// KsConfig starts at v5, so a "ver" of 5 (or anything outside 1..4) is not a legacy Touch
// blob and this migration must decline it — the native decode path owns v5.
void test_a_non_legacy_version_is_declined(void) {
    KTouchLegacyV4 v4;
    memset(&v4, 0, sizeof(v4));
    KsConfig out;
    TEST_ASSERT_FALSE(ktouch_migrate_legacy_config(&out, &v4, sizeof(v4), 5u));
    TEST_ASSERT_FALSE(ktouch_migrate_legacy_config(&out, &v4, sizeof(v4), 0u));
}

// Bit-rot in a correctly-sized, correctly-versioned blob still fails the validity gate,
// and a rejected migration hands back defaults, not a half-migrated struct.
void test_bit_rot_fails_validation_and_falls_back_to_defaults(void) {
    KTouchLegacyV4 v4;
    memset(&v4, 0, sizeof(v4));
    v4.quantum_beats = 999;   // out of 1..64
    v4.clock_enable = 1; v4.transport_enable = 1; v4.play_on_release = 0;
    v4.nudge_mbeats = 0; v4.brightness = 80; v4.ppqn = 24; v4.swing_mbeats = 0;
    v4.tempo_mbpm = 120000;

    KsConfig out;
    TEST_ASSERT_FALSE(ktouch_migrate_legacy_config(&out, &v4, sizeof(v4), 4u));
    TEST_ASSERT_EQUAL_INT(4, out.quantum_beats);   // default, not the rotten 999
    TEST_ASSERT_TRUE(ks_config_valid(&out));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_v4_blob_migrates_every_field_into_ksconfig);
    RUN_TEST(test_v3_blob_defaults_the_tempo_the_layout_lacked);
    RUN_TEST(test_v2_blob_defaults_ppqn_swing_and_tempo);
    RUN_TEST(test_v1_blob_puts_the_one_credential_in_slot_zero);
    RUN_TEST(test_a_wrong_size_blob_for_its_version_is_rejected);
    RUN_TEST(test_a_non_legacy_version_is_declined);
    RUN_TEST(test_bit_rot_fails_validation_and_falls_back_to_defaults);
    return UNITY_END();
}
