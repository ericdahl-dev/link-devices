// Host tests for the pure GET /config.json builder (P4-041).
#include "unity.h"
#include "ks_config.h"
#include "ks_config_json.h"
#include <string.h>

static KsConfig c;
void setUp(void)    { ks_config_defaults(&c); }
void tearDown(void) {}

void test_defaults_round_trip_the_documented_keys(void) {
    char b[768];
    ks_config_json(b, sizeof(b), &c);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"clock_out\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"metronome\":false"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"metro_accent\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"metro_vol\":80"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"metro_voice\":0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"led\":false"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"led_bright\":60"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"led_beat\":\"#00B400\""));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"led_accent\":\"#DC6E00\""));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow_beat\":false"));
}

// Field names match the POST /save and /live grammar (ks_config_set() in
// ks_config.c) exactly -- a client reads and writes through one shared name per
// field, never two.
void test_field_names_match_the_form_grammar(void) {
    char b[768];
    strncpy(c.wifi[0].ssid, "TestNet", sizeof(c.wifi[0].ssid) - 1);
    ks_config_json(b, sizeof(b), &c);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"cable\":0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"ppqn\":24"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"phase\":0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"swing\":0"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"follow\":true"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"ssid\":\"TestNet\""));
}

// Output 0 is on by default (24-PPQN MIDI clock on cable 0), the rest off --
// same defaults ks_config_defaults() sets, just visible through the JSON now.
void test_clock_array_has_four_outputs_in_order(void) {
    char b[768];
    c.clock[2].cable = 3;
    c.clock[2].ppqn = 4;
    ks_config_json(b, sizeof(b), &c);
    const char* clock_array = strstr(b, "\"clock\":[");
    TEST_ASSERT_NOT_NULL(clock_array);
    const char* third = strstr(clock_array, "\"cable\":3");
    TEST_ASSERT_NOT_NULL(third);
    // Comes after exactly two prior '{' output objects, not the first or last.
    int opens_before = 0;
    for (const char* p = clock_array; p < third; p++) if (*p == '{') opens_before++;
    TEST_ASSERT_EQUAL_INT(3, opens_before);
}

// SECURITY: a saved password is NEVER echoed, only whether one is set -- the
// same rule ks_web.cpp's build_wifi() already applies to the HTML form.
void test_wifi_password_is_never_included(void) {
    char b[768];
    strncpy(c.wifi[0].ssid, "TestNet", sizeof(c.wifi[0].ssid) - 1);
    strncpy(c.wifi[0].pass, "supersecret", sizeof(c.wifi[0].pass) - 1);
    ks_config_json(b, sizeof(b), &c);
    TEST_ASSERT_NULL(strstr(b, "supersecret"));
    TEST_ASSERT_NOT_NULL(strstr(b, "\"pass_set\":true"));
}

void test_wifi_pass_set_false_when_slot_empty(void) {
    char b[768];
    ks_config_json(b, sizeof(b), &c);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"pass_set\":false"));
}

// Same truncation-detection convention as ks_status_json(): the return value is
// still comparable against the buffer size even when the buffer was too small.
void test_return_value_is_snprintf_style_length(void) {
    char b[768];
    int n = ks_config_json(b, sizeof(b), &c);
    TEST_ASSERT_EQUAL_INT((int)strlen(b), n);

    char tiny[8];
    int n2 = ks_config_json(tiny, sizeof(tiny), &c);
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);
}

// A buffer sized like the one /status uses today should comfortably fit the full
// config -- this is the number a real ks_web.cpp handler needs to pick a buffer
// size, the same headroom concern P4-038 flagged for ks_status_json's buffer.
void test_fits_in_a_generously_sized_stack_buffer(void) {
    char b[768];
    // Worst case: every field maxed toward its longest legal value.
    for (int i = 0; i < KS_WIFI_SLOTS; i++) {
        memset(c.wifi[i].ssid, 'A', sizeof(c.wifi[i].ssid) - 1);
        memset(c.wifi[i].pass, 'B', sizeof(c.wifi[i].pass) - 1);
    }
    for (int i = 0; i < KS_CLOCK_OUTPUTS; i++) {
        c.clock[i].cable = 3;
        c.clock[i].ppqn = 48;
        c.clock[i].phase_mbeats = -250;
        c.clock[i].swing_mbeats = 250;
    }
    int n = ks_config_json(b, sizeof(b), &c);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < (int)sizeof(b));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_round_trip_the_documented_keys);
    RUN_TEST(test_field_names_match_the_form_grammar);
    RUN_TEST(test_clock_array_has_four_outputs_in_order);
    RUN_TEST(test_wifi_password_is_never_included);
    RUN_TEST(test_wifi_pass_set_false_when_slot_empty);
    RUN_TEST(test_return_value_is_snprintf_style_length);
    RUN_TEST(test_fits_in_a_generously_sized_stack_buffer);
    return UNITY_END();
}
