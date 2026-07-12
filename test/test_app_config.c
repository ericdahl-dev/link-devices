#include "unity.h"
#include "app_config.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

void test_defaults_model_is_xr18(void) {
    AppConfig cfg;
    config_defaults(&cfg);
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, cfg.model);
}

void test_defaults_fx_slot_is_1(void) {
    AppConfig cfg;
    config_defaults(&cfg);
    TEST_ASSERT_EQUAL_INT(1, cfg.fx_slot);
}

void test_defaults_ip_set(void) {
    AppConfig cfg;
    config_defaults(&cfg);
    TEST_ASSERT_TRUE(cfg.mixer_ip[0] != '\0');
}

void test_xr18_port(void) {
    TEST_ASSERT_EQUAL_INT(10024, config_model_port(MODEL_XR18));
}

void test_x32_port(void) {
    TEST_ASSERT_EQUAL_INT(10023, config_model_port(MODEL_X32));
}

void test_xr18_slot_max(void) {
    TEST_ASSERT_EQUAL_INT(4, config_model_slot_max(MODEL_XR18));
}

void test_x32_slot_max(void) {
    TEST_ASSERT_EQUAL_INT(8, config_model_slot_max(MODEL_X32));
}

void test_validate_rejects_slot_zero(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.fx_slot = 0;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_rejects_slot_above_max_xr18(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model   = MODEL_XR18;
    cfg.fx_slot = 5;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_rejects_slot_above_max_x32(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model   = MODEL_X32;
    cfg.fx_slot = 9;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_rejects_empty_ip(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.mixer_ip[0] = '\0';
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_accepts_valid_xr18(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model   = MODEL_XR18;
    cfg.fx_slot = 4;
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_validate_accepts_valid_x32(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model   = MODEL_X32;
    cfg.fx_slot = 8;
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_defaults_input_source_is_link(void) {
    AppConfig cfg; config_defaults(&cfg);
    TEST_ASSERT_EQUAL_INT(0, cfg.input_source);  // 0 = Ableton Link
}

void test_validate_accepts_midi_source(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.input_source = 1;  // USB MIDI
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_validate_rejects_source_above_range(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.input_source = 2;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_rejects_negative_source(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.input_source = -1;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_defaults_fdr_chan_is_32(void) {
    AppConfig cfg; config_defaults(&cfg);
    TEST_ASSERT_EQUAL_INT(32, cfg.fdr_chan_count);
}

void test_validate_rejects_bad_chan_count(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.fdr_chan_count = 17;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_accepts_chan_16(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.fdr_chan_count = 16;
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_defaults_quantum_beats_is_4(void) {
    AppConfig cfg; config_defaults(&cfg);
    TEST_ASSERT_EQUAL_INT(4, cfg.quantum_beats);
}

void test_validate_rejects_quantum_beats_zero(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = 0;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_rejects_quantum_beats_above_16(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = 17;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_accepts_quantum_beats_1(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = 1;
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_validate_accepts_quantum_beats_16(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = 16;
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_validate_rejects_negative_quantum_beats(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = -1;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

// LNK-032: config_set_model owns the model→slot dependency for both editors.
void test_set_model_x32_keeps_valid_high_slot(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model = MODEL_X32; cfg.fx_slot = 8;
    config_set_model(&cfg, MODEL_X32);
    TEST_ASSERT_EQUAL_INT(MODEL_X32, cfg.model);
    TEST_ASSERT_EQUAL_INT(8, cfg.fx_slot);          // 8 is valid on X32
}

void test_set_model_xr18_clamps_slot_to_max(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model = MODEL_X32; cfg.fx_slot = 8;
    config_set_model(&cfg, MODEL_XR18);
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, cfg.model);
    TEST_ASSERT_EQUAL_INT(4, cfg.fx_slot);          // clamped 8 -> 4
}

void test_set_model_ignores_invalid_model(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model = MODEL_X32; cfg.fx_slot = 7;
    config_set_model(&cfg, 99);
    TEST_ASSERT_EQUAL_INT(MODEL_X32, cfg.model);     // unchanged
    TEST_ASSERT_EQUAL_INT(7, cfg.fx_slot);
}

// LNK-036: phase display mode + dot colours.
void test_defaults_phase_display_is_sweep(void) {
    AppConfig cfg; config_defaults(&cfg);
    TEST_ASSERT_EQUAL_INT(0, cfg.phase_display_mode);          // 0 = sweep wheel
    TEST_ASSERT_EQUAL_HEX32(0xB6FF36, cfg.dot_beat_color);     // P4 parity
    TEST_ASSERT_EQUAL_HEX32(0xFF9D3B, cfg.dot_accent_color);
}

void test_validate_accepts_flash_mode(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.phase_display_mode = 1;                                // beat-flash dot
    TEST_ASSERT_TRUE(config_validate(&cfg));
}

void test_validate_rejects_bad_phase_mode(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.phase_display_mode = 2;
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

void test_validate_rejects_out_of_range_dot_color(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.dot_beat_color = 0x1000000;                            // > 0xFFFFFF
    TEST_ASSERT_FALSE(config_validate(&cfg));
}

// ARC-012: app_config_set — validated setter, one range owner (config_validate).
void test_set_applies_valid_value(void) {
    AppConfig cfg; config_defaults(&cfg);
    TEST_ASSERT_TRUE(app_config_set(&cfg, ACF_QUANTUM_BEATS, 8));
    TEST_ASSERT_EQUAL_INT(8, cfg.quantum_beats);
}

void test_set_rejects_out_of_range_keeps_old(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = 4;
    TEST_ASSERT_FALSE(app_config_set(&cfg, ACF_QUANTUM_BEATS, 17));   // > 16
    TEST_ASSERT_EQUAL_INT(4, cfg.quantum_beats);                      // unchanged
}

void test_set_quantum_incdec_bounds_via_setter(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.quantum_beats = 16;
    TEST_ASSERT_FALSE(app_config_set(&cfg, ACF_QUANTUM_BEATS, cfg.quantum_beats + 1)); // 17 rejected
    TEST_ASSERT_EQUAL_INT(16, cfg.quantum_beats);                    // inc stops at max
}

void test_set_model_clamps_slot_through_shared_helper(void) {
    AppConfig cfg; config_defaults(&cfg);
    cfg.model = MODEL_X32; cfg.fx_slot = 8;
    TEST_ASSERT_TRUE(app_config_set(&cfg, ACF_MODEL, MODEL_XR18));
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, cfg.model);
    TEST_ASSERT_EQUAL_INT(4, cfg.fx_slot);                          // clamped 8 -> 4
}

void test_set_rejects_bad_input_source(void) {
    AppConfig cfg; config_defaults(&cfg);
    TEST_ASSERT_FALSE(app_config_set(&cfg, ACF_INPUT_SOURCE, 2));
    TEST_ASSERT_EQUAL_INT(0, cfg.input_source);
}

// ---------------------------------------------------------------- ARC-020
// config_decode: the one owner of "is this persisted blob safe to load?".
// Mirrors test_ks_config.c. Every gate is fail-closed -> defaults.

// A blob written by this build decodes back verbatim.
void test_decode_round_trip(void) {
    AppConfig saved; config_defaults(&saved);
    TEST_ASSERT_TRUE(app_config_set(&saved, ACF_MODEL, MODEL_X32));
    TEST_ASSERT_TRUE(app_config_set(&saved, ACF_FX_SLOT, 7));       // X32-only slot
    TEST_ASSERT_TRUE(app_config_set(&saved, ACF_QUANTUM_BEATS, 8));
    strcpy(saved.wifi_ssid, "studio");
    strcpy(saved.mixer_ip,  "10.0.0.9");

    AppConfig out;
    cfg_decode_result r = config_decode(&out, &saved, sizeof(saved),
                                        true, APP_CONFIG_VERSION);
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_OK, r);
    TEST_ASSERT_EQUAL_INT(MODEL_X32, out.model);
    TEST_ASSERT_EQUAL_INT(7, out.fx_slot);
    TEST_ASSERT_EQUAL_INT(8, out.quantum_beats);
    TEST_ASSERT_EQUAL_STRING("studio",   out.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("10.0.0.9", out.mixer_ip);
}

// No blob at all (fresh flash) -> defaults.
void test_decode_absent_blob_defaults(void) {
    AppConfig out;
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED, config_decode(&out, NULL, 0, false, 0));
    TEST_ASSERT_TRUE(config_validate(&out));
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, out.model);   // == defaults
}

// Bytes present but no version key (predates versioning) -> not vouched for.
void test_decode_no_version_defaults(void) {
    AppConfig saved; config_defaults(&saved);
    app_config_set(&saved, ACF_MODEL, MODEL_X32);

    AppConfig out;
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED,
                          config_decode(&out, &saved, sizeof(saved), false, 0));
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, out.model);   // NOT X32
}

// A version we don't know -> defaults (never guess a layout).
void test_decode_wrong_version_defaults(void) {
    AppConfig saved; config_defaults(&saved);
    app_config_set(&saved, ACF_MODEL, MODEL_X32);

    AppConfig out;
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED,
                          config_decode(&out, &saved, sizeof(saved), true,
                                        APP_CONFIG_VERSION + 99u));
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, out.model);
}

// Right version, wrong size -> the struct moved under us. Defaults.
void test_decode_size_mismatch_defaults(void) {
    AppConfig saved; config_defaults(&saved);
    app_config_set(&saved, ACF_MODEL, MODEL_X32);

    AppConfig out;
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED,
                          config_decode(&out, &saved, sizeof(saved) - 4, true,
                                        APP_CONFIG_VERSION));
    TEST_ASSERT_EQUAL_INT(MODEL_XR18, out.model);
}

// Version and size both vouch for it, but a field is out of range (bit-rot, or a
// layout shipped without a version bump). The guard still runs. Defaults.
void test_decode_invalid_contents_defaults(void) {
    AppConfig saved; config_defaults(&saved);
    saved.mixer_ip[0] = '\0';     // config_validate rejects an empty mixer IP

    AppConfig out;
    TEST_ASSERT_EQUAL_INT(CFG_DECODE_DEFAULTED,
                          config_decode(&out, &saved, sizeof(saved), true,
                                        APP_CONFIG_VERSION));
    TEST_ASSERT_TRUE(config_validate(&out));
    TEST_ASSERT_TRUE(out.mixer_ip[0] != '\0');
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_round_trip);
    RUN_TEST(test_decode_absent_blob_defaults);
    RUN_TEST(test_decode_no_version_defaults);
    RUN_TEST(test_decode_wrong_version_defaults);
    RUN_TEST(test_decode_size_mismatch_defaults);
    RUN_TEST(test_decode_invalid_contents_defaults);
    RUN_TEST(test_set_applies_valid_value);
    RUN_TEST(test_set_rejects_out_of_range_keeps_old);
    RUN_TEST(test_set_quantum_incdec_bounds_via_setter);
    RUN_TEST(test_set_model_clamps_slot_through_shared_helper);
    RUN_TEST(test_set_rejects_bad_input_source);
    RUN_TEST(test_defaults_phase_display_is_sweep);
    RUN_TEST(test_validate_accepts_flash_mode);
    RUN_TEST(test_validate_rejects_bad_phase_mode);
    RUN_TEST(test_validate_rejects_out_of_range_dot_color);
    RUN_TEST(test_set_model_x32_keeps_valid_high_slot);
    RUN_TEST(test_set_model_xr18_clamps_slot_to_max);
    RUN_TEST(test_set_model_ignores_invalid_model);
    RUN_TEST(test_defaults_fdr_chan_is_32);
    RUN_TEST(test_validate_rejects_bad_chan_count);
    RUN_TEST(test_validate_accepts_chan_16);
    RUN_TEST(test_defaults_quantum_beats_is_4);
    RUN_TEST(test_validate_rejects_quantum_beats_zero);
    RUN_TEST(test_validate_rejects_quantum_beats_above_16);
    RUN_TEST(test_validate_accepts_quantum_beats_1);
    RUN_TEST(test_validate_accepts_quantum_beats_16);
    RUN_TEST(test_validate_rejects_negative_quantum_beats);
    RUN_TEST(test_defaults_model_is_xr18);
    RUN_TEST(test_defaults_fx_slot_is_1);
    RUN_TEST(test_defaults_ip_set);
    RUN_TEST(test_xr18_port);
    RUN_TEST(test_x32_port);
    RUN_TEST(test_xr18_slot_max);
    RUN_TEST(test_x32_slot_max);
    RUN_TEST(test_validate_rejects_slot_zero);
    RUN_TEST(test_validate_rejects_slot_above_max_xr18);
    RUN_TEST(test_validate_rejects_slot_above_max_x32);
    RUN_TEST(test_validate_rejects_empty_ip);
    RUN_TEST(test_validate_accepts_valid_xr18);
    RUN_TEST(test_validate_accepts_valid_x32);
    RUN_TEST(test_defaults_input_source_is_link);
    RUN_TEST(test_validate_accepts_midi_source);
    RUN_TEST(test_validate_rejects_source_above_range);
    RUN_TEST(test_validate_rejects_negative_source);
    return UNITY_END();
}
