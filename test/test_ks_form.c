// Host tests for the pure KitchenSync POST-body resolver (ARC-006).
// ks_form_resolve owns URL-decode + pair-split + checkbox-absence -> off,
// replaying each pair through the tested ks_config_set grammar. These are
// the bits that used to live untested inside save_handler.
#include "unity.h"
#include "ks_form.h"
#include "ks_config.h"
#include <string.h>

static KsConfig base, out;
void setUp(void)    { ks_config_defaults(&base); }
void tearDown(void) {}

// resolve() takes a MUTABLE body (it decodes + tokenizes in place), so every
// test copies its literal into a scratch buffer first.
static void resolve(const char *body) {
    char buf[1024];
    strncpy(buf, body, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    ks_form_resolve(buf, &base, &out);
}

// Tracer: a single value field lands, everything else copied from base.
void test_sets_one_field_keeps_base(void) {
    resolve("wifi_ssid=Studio");
    TEST_ASSERT_EQUAL_STRING("Studio", out.wifi_ssid);
    TEST_ASSERT_EQUAL_INT(80, out.metronome_volume);   // untouched -> from base
}

// The whole point of the module: an unchecked checkbox is absent from the body,
// which must read as "off" even though base had it on.
void test_absent_checkbox_reads_off(void) {
    base.clock_out_enable = 1;
    base.metronome_enable = 1;
    base.metronome_accent = 1;
    resolve("wifi_ssid=X");              // none of the checkbox keys present
    TEST_ASSERT_EQUAL_INT(0, out.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(0, out.metronome_enable);
    TEST_ASSERT_EQUAL_INT(0, out.metronome_accent);
}

// A present checkbox flips back on.
void test_present_checkbox_reads_on(void) {
    base.metronome_enable = 0;
    resolve("metronome=1&clock_out=1&metro_accent=1");
    TEST_ASSERT_EQUAL_INT(1, out.metronome_enable);
    TEST_ASSERT_EQUAL_INT(1, out.clock_out_enable);
    TEST_ASSERT_EQUAL_INT(1, out.metronome_accent);
}

// Per-output enables are checkboxes too: absent clk<N>_en -> that output off,
// present -> on. This is the grammar that used to be duplicated in save_handler.
void test_per_output_enable_absence(void) {
    base.clock[0].enable = 1;   // on in base
    base.clock[1].enable = 0;
    resolve("clk1_en=1");       // 0 absent -> off; 1 present -> on
    TEST_ASSERT_EQUAL_INT(0, out.clock[0].enable);
    TEST_ASSERT_EQUAL_INT(1, out.clock[1].enable);
}

// A NON-checkbox field absent from the body keeps its base value (not cleared).
void test_absent_value_field_keeps_base(void) {
    base.metronome_volume = 65;
    base.clock[0].ppqn    = 48;
    resolve("wifi_ssid=X");
    TEST_ASSERT_EQUAL_INT(65, out.metronome_volume);
    TEST_ASSERT_EQUAL_INT(48, out.clock[0].ppqn);
}

// URL-decode: %XX and '+' in a value.
void test_url_decode_value(void) {
    resolve("wifi_ssid=My+Home%20Net");
    TEST_ASSERT_EQUAL_STRING("My Home Net", out.wifi_ssid);
}

// A %26 inside a token decodes to '&' AFTER the split, so it doesn't act as a
// pair separator — the classic reason decode must run per-token, not up front.
void test_encoded_ampersand_survives_split(void) {
    resolve("wifi_pass=a%26b");
    TEST_ASSERT_EQUAL_STRING("a&b", out.wifi_pass);
}

// --- ks_form_apply: PATCH semantics for POST /live ---------------------

static void apply(const char *body) {
    char buf[1024];
    strncpy(buf, body, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    ks_form_apply(buf, &base, &out);
}

// A present field is applied; an absent CHECKBOX is left alone (not cleared).
// This is the difference from resolve() — a live nudge that carries only a phase
// value must not disable clock-out or the metronome.
void test_apply_patches_present_leaves_absent_checkbox(void) {
    base.clock_out_enable = 1;
    base.metronome_enable = 1;
    base.metronome_accent = 1;
    base.clock[0].enable  = 1;
    apply("clk0_phase=40");
    TEST_ASSERT_EQUAL_INT(40, out.clock[0].phase_mbeats);   // applied
    TEST_ASSERT_EQUAL_INT(1,  out.clock_out_enable);        // untouched
    TEST_ASSERT_EQUAL_INT(1,  out.metronome_enable);
    TEST_ASSERT_EQUAL_INT(1,  out.metronome_accent);
    TEST_ASSERT_EQUAL_INT(1,  out.clock[0].enable);
}

// A present checkbox key still turns it on (patch can enable, just can't
// implicitly disable via absence).
void test_apply_present_checkbox_still_sets(void) {
    base.metronome_accent = 0;
    apply("metro_accent=1");
    TEST_ASSERT_EQUAL_INT(1, out.metronome_accent);
}

// Absent value fields keep their base value, same as resolve.
void test_apply_absent_value_keeps_base(void) {
    base.clock[1].ppqn = 48;
    apply("clk0_phase=10");
    TEST_ASSERT_EQUAL_INT(48, out.clock[1].ppqn);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sets_one_field_keeps_base);
    RUN_TEST(test_absent_checkbox_reads_off);
    RUN_TEST(test_present_checkbox_reads_on);
    RUN_TEST(test_per_output_enable_absence);
    RUN_TEST(test_absent_value_field_keeps_base);
    RUN_TEST(test_url_decode_value);
    RUN_TEST(test_encoded_ampersand_survives_split);
    RUN_TEST(test_apply_patches_present_leaves_absent_checkbox);
    RUN_TEST(test_apply_present_checkbox_still_sets);
    RUN_TEST(test_apply_absent_value_keeps_base);
    return UNITY_END();
}
