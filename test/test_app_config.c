#include "unity.h"
#include "app_config.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// ESP-040: THE BUG. X32Link's handle_save read input_source / midi_clock_out /
// phase_flash UNCONDITIONALLY, and an absent key decodes to 0 — a LEGAL value for all
// three. So a partial POST (the iOS app saving a WiFi network, a subset form) silently
// reset the tempo source to Link, disabled the MIDI clock, and flipped the phase display
// to sweep. A patch (full_form=false) must leave absent fields ALONE.
void test_partial_post_preserves_legal_zero_fields(void) {
    AppConfig base; config_defaults(&base);
    app_config_set(&base, ACF_INPUT_SOURCE, 1);        // USB MIDI clock (legal 1)
    app_config_set(&base, ACF_MIDI_CLOCK_OUT, 1);      // clock out ON
    app_config_set(&base, ACF_PHASE_DISPLAY_MODE, 1);  // beat-flash dot

    // The app saves only a WiFi network — nothing about the clock or phase.
    const X32FormField fields[] = { {"wifi_ssid", "StudioNet"} };
    AppConfig out;
    x32_form_merge(&out, &base, fields, 1, /*full_form=*/false);

    TEST_ASSERT_EQUAL_INT(1, out.input_source);         // NOT reset to Link
    TEST_ASSERT_EQUAL_INT(1, out.midi_clock_out_enable);// NOT disabled
    TEST_ASSERT_EQUAL_INT(1, out.phase_display_mode);   // NOT flipped to sweep
    TEST_ASSERT_EQUAL_STRING("StudioNet", out.wifi_ssid);
}

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

// The device's OWN page posts the whole form (full_form=true). An unchecked checkbox
// sends nothing, so its absence MUST mean off — that's the only way the page can turn one
// off. This is the flip side of the partial-POST rule and must not disable it.
void test_full_form_absent_checkbox_turns_off(void) {
    AppConfig base; config_defaults(&base);
    app_config_set(&base, ACF_MIDI_CLOCK_OUT, 1);
    app_config_set(&base, ACF_PHASE_DISPLAY_MODE, 1);

    // A full form with the two checkboxes UNCHECKED (absent), value fields present.
    const X32FormField fields[] = {
        {"input_source", "0"}, {"model", "1"}, {"fx_slot", "1"}, {"mixer_ip", "192.168.0.2"},
    };
    AppConfig out;
    x32_form_merge(&out, &base, fields, 4, /*full_form=*/true);
    TEST_ASSERT_EQUAL_INT(0, out.midi_clock_out_enable);   // absent checkbox -> off
    TEST_ASSERT_EQUAL_INT(0, out.phase_display_mode);
}

void test_full_form_present_checkbox_stays_on(void) {
    AppConfig base; config_defaults(&base);
    const X32FormField fields[] = { {"midi_clock_out", "1"}, {"phase_flash", "1"} };
    AppConfig out;
    x32_form_merge(&out, &base, fields, 2, /*full_form=*/true);
    TEST_ASSERT_EQUAL_INT(1, out.midi_clock_out_enable);   // present -> back on after pre-clear
    TEST_ASSERT_EQUAL_INT(1, out.phase_display_mode);
}

// A full form must NOT zero a VALUE field just because it's a legal 0 — only genuine
// checkboxes get the absent-means-off treatment. input_source is a hidden value field
// (always sent), so full-form pre-clear must never touch it.
void test_full_form_does_not_zero_the_value_fields(void) {
    AppConfig base; config_defaults(&base);
    app_config_set(&base, ACF_INPUT_SOURCE, 1);   // USB MIDI
    // A full form that (hypothetically) omitted input_source must keep it, not clear it —
    // the pre-clear list is checkboxes only.
    const X32FormField fields[] = { {"midi_clock_out", "1"} };
    AppConfig out;
    x32_form_merge(&out, &base, fields, 1, /*full_form=*/true);
    TEST_ASSERT_EQUAL_INT(1, out.input_source);   // not a checkbox — never zeroed
}

// ESP-040 round-trip: the SHARED keys X32Link's /config.json publishes (clock_out,
// led_beat, led_accent) must be accepted by /save under the SAME names, so a client can
// read the config and write it back. Before this they were bespoke (midi_clock_out,
// dot_beat, dot_acc) and a round-trip was impossible.
void test_config_json_shared_keys_round_trip(void) {
    AppConfig base; config_defaults(&base);
    const X32FormField fields[] = {
        {"clock_out",  "1"},        // /config.json's name for midi_clock_out_enable
        {"led_beat",   "#112233"},  // dot_beat_color
        {"led_accent", "#445566"},  // dot_accent_color
    };
    AppConfig out;
    x32_form_merge(&out, &base, fields, 3, /*full_form=*/false);
    TEST_ASSERT_EQUAL_INT(1, out.midi_clock_out_enable);
    TEST_ASSERT_EQUAL_INT(0x112233, out.dot_beat_color);
    TEST_ASSERT_EQUAL_INT(0x445566, out.dot_accent_color);
}

void test_kv_blank_password_keeps_current(void) {
    AppConfig cfg; config_defaults(&cfg);
    strcpy(cfg.wifi_pass, "downbeat99");
    TEST_ASSERT_TRUE(app_config_set_kv(&cfg, "wifi_pass", ""));   // blank = keep
    TEST_ASSERT_EQUAL_STRING("downbeat99", cfg.wifi_pass);
}

void test_kv_unknown_key_changes_nothing(void) {
    AppConfig cfg; config_defaults(&cfg);
    AppConfig before = cfg;
    TEST_ASSERT_FALSE(app_config_set_kv(&cfg, "not_a_field", "9"));
    TEST_ASSERT_EQUAL_INT(0, memcmp(&before, &cfg, sizeof(cfg)));
}

// An out-of-range value is rejected by config_validate (via app_config_set) and leaves the
// field untouched — bit-rot in the form never reaches the mixer.
void test_kv_over_range_value_is_rejected(void) {
    AppConfig cfg; config_defaults(&cfg);
    int before = cfg.quantum_beats;
    TEST_ASSERT_FALSE(app_config_set_kv(&cfg, "quantum", "99"));   // max is 16
    TEST_ASSERT_EQUAL_INT(before, cfg.quantum_beats);
}

// Regression guard: fx_slot validates against the model's slot max, so `model` must be
// applied BEFORE `fx_slot` regardless of POST order. Here fx_slot=8 (valid only on X32)
// appears in the array BEFORE model=X32 — a naive in-order merge would reject the 8
// against the base XR18 (max 4) and silently drop it. The device's own full form relies
// on this pairing every save.
void test_merge_applies_model_before_fx_slot(void) {
    AppConfig base; config_defaults(&base);   // XR18, slot 1
    const X32FormField fields[] = {
        {"fx_slot", "8"},        // deliberately before model in the array
        {"model",   "2"},        // MODEL_X32 (slot max 8)
        {"mixer_ip","192.168.0.2"},
    };
    AppConfig out;
    x32_form_merge(&out, &base, fields, 3, /*full_form=*/true);
    TEST_ASSERT_EQUAL_INT(MODEL_X32, out.model);
    TEST_ASSERT_EQUAL_INT(8, out.fx_slot);   // not dropped to 1 by an ordering accident
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
    RUN_TEST(test_partial_post_preserves_legal_zero_fields);
    RUN_TEST(test_full_form_absent_checkbox_turns_off);
    RUN_TEST(test_full_form_present_checkbox_stays_on);
    RUN_TEST(test_full_form_does_not_zero_the_value_fields);
    RUN_TEST(test_config_json_shared_keys_round_trip);
    RUN_TEST(test_kv_blank_password_keeps_current);
    RUN_TEST(test_kv_unknown_key_changes_nothing);
    RUN_TEST(test_kv_over_range_value_is_rejected);
    RUN_TEST(test_merge_applies_model_before_fx_slot);
    return UNITY_END();
}
