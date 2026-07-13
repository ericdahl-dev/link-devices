#include "unity.h"
#include "web_status_json.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// bpm/phase/valid/quantum/fw all show up, valid serializes as a real JSON bool.
// has_batt=false — no battery fields in the payload.
void test_valid_reading_includes_all_fields(void) {
    char buf[128];
    web_status_json(buf, sizeof(buf), 120.0f, 1.2500f, true, 4, "2.1.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":120.0,\"phase\":1.2500,\"valid\":true,\"quantum\":4,\"fw\":\"2.1.0\"}", buf);
}

// No phase reading yet (tempo_source_phase()'s -1.0f sentinel) — still
// printed as-is; `valid:false` is what JS actually gates on, per LNK-022.
void test_invalid_reading_passes_phase_through_unspecial_cased(void) {
    char buf[128];
    web_status_json(buf, sizeof(buf), 0.0f, -1.0f, false, 4, "2.1.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":0.0,\"phase\":-1.0000,\"valid\":false,\"quantum\":4,\"fw\":\"2.1.0\"}", buf);
}

// quantum_beats round-trips at both ends of its valid range (1-16, LNK-019).
void test_quantum_min_and_max(void) {
    char buf[128];
    web_status_json(buf, sizeof(buf), 100.0f, 0.0f, true, 1, "2.1.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":100.0,\"phase\":0.0000,\"valid\":true,\"quantum\":1,\"fw\":\"2.1.0\"}", buf);
    web_status_json(buf, sizeof(buf), 100.0f, 15.9999f, true, 16, "2.1.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":100.0,\"phase\":15.9999,\"valid\":true,\"quantum\":16,\"fw\":\"2.1.0\"}", buf);
}

// LNK-038: the fw string is caller-supplied (the pure builder stays
// version-agnostic — glue passes FW_VERSION from fw_version.h).
void test_fw_string_passes_through(void) {
    char buf[128];
    web_status_json(buf, sizeof(buf), 120.0f, 0.0f, true, 4, "9.9.9-rc1", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"fw\":\"9.9.9-rc1\""));
}

// Return value mirrors snprintf's contract — bytes that would've been
// written, so a too-small buffer is detectable by the caller.
void test_return_value_is_snprintf_style_length(void) {
    char buf[128];
    int n = web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.1.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);

    char tiny[4];
    int n2 = web_status_json(tiny, sizeof(tiny), 120.0f, 1.25f, true, 4, "2.1.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);  // truncated but length still reported
}

// has_batt=true adds batt_v/batt_pct to the payload (LiPo BFF-equipped boards).
void test_battery_fields_included_when_present(void) {
    char buf[128];
    web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.2.0", true, 3.85f, 62.5f, NULL);
    TEST_ASSERT_EQUAL_STRING(
        "{\"bpm\":120.0,\"phase\":1.2500,\"valid\":true,\"quantum\":4,\"fw\":\"2.2.0\","
        "\"batt_v\":3.85,\"batt_pct\":62.5}", buf);
}

// has_batt=false omits both fields regardless of the values passed —
// boards with no fuel gauge keep the old schema exactly.
void test_battery_fields_omitted_when_absent(void) {
    char buf[128];
    web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.2.0", false, 3.85f, 62.5f, NULL);
    TEST_ASSERT_NULL(strstr(buf, "batt_v"));
    TEST_ASSERT_NULL(strstr(buf, "batt_pct"));
}

// ARC-024: the tick-health block. X32Link had NO tick probe at all -- the one
// firmware never measured, on the same S3 silicon, with the same on-die WiFi and
// the same lwIP priority that produced ESP-018 on the Touch. These are the numbers
// that make the question askable: gap (the task was never scheduled) vs work
// (something inside the tick blocked), and `drop` -- pulses the realign THREW AWAY,
// which leave no burst and no gap on the wire and are otherwise undetectable.
void test_tick_health_fields_included_when_present(void) {
    char buf[256];
    WebTickHealth t = { .dropped = 7, .bursts = 2, .max_gap_us = 5038, .max_work_us = 124,
                        .overruns = 3, .w_beats = 90, .w_clock = 34, .core = 1, .reprimes = 9 };
    web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.2.0", false, 0.0f, 0.0f, &t);
    TEST_ASSERT_EQUAL_STRING(
        "{\"bpm\":120.0,\"phase\":1.2500,\"valid\":true,\"quantum\":4,\"fw\":\"2.2.0\","
        "\"drop\":7,\"burst\":2,\"gap\":5038,\"work\":124,\"over\":3,\"core\":1,"
        "\"w_beats\":90,\"w_clock\":34,\"reprime\":9}", buf);
}

// NULL omits the whole block -- a build with no probe keeps the old schema byte for
// byte, exactly as has_batt already does for the fuel gauge.
void test_tick_health_fields_omitted_when_null(void) {
    char buf[256];
    web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.2.0", false, 0.0f, 0.0f, NULL);
    TEST_ASSERT_EQUAL_STRING(
        "{\"bpm\":120.0,\"phase\":1.2500,\"valid\":true,\"quantum\":4,\"fw\":\"2.2.0\"}", buf);
}

// Battery and tick health are independent options, so both at once must still produce
// ONE well-formed object -- the case the old two-branch snprintf could not have made.
void test_battery_and_tick_health_together(void) {
    char buf[256];
    WebTickHealth t = { .dropped = 0, .bursts = 0, .max_gap_us = 1002, .max_work_us = 62,
                        .overruns = 0, .w_beats = 41, .w_clock = 12, .core = 1 };
    web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.2.0", true, 3.85f, 62.5f, &t);
    TEST_ASSERT_EQUAL_STRING(
        "{\"bpm\":120.0,\"phase\":1.2500,\"valid\":true,\"quantum\":4,\"fw\":\"2.2.0\","
        "\"batt_v\":3.85,\"batt_pct\":62.5,"
        "\"drop\":0,\"burst\":0,\"gap\":1002,\"work\":62,\"over\":0,\"core\":1,"
        "\"w_beats\":41,\"w_clock\":12,\"reprime\":0}", buf);
}

// Truncation stays snprintf-style with the block on: the caller can still detect it,
// and a too-small buffer must still come back NUL-terminated.
void test_tick_health_truncation_is_reported(void) {
    char tiny[24];
    WebTickHealth t = { .dropped = 1, .bursts = 1, .max_gap_us = 1, .max_work_us = 1,
                        .overruns = 1, .w_beats = 1, .w_clock = 1, .core = 0 };
    int n = web_status_json(tiny, sizeof(tiny), 120.0f, 1.25f, true, 4, "2.2.0", false, 0.0f, 0.0f, &t);
    TEST_ASSERT_TRUE(n > (int)sizeof(tiny) - 1);
    TEST_ASSERT_EQUAL_CHAR('\0', tiny[sizeof(tiny) - 1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_reading_includes_all_fields);
    RUN_TEST(test_invalid_reading_passes_phase_through_unspecial_cased);
    RUN_TEST(test_quantum_min_and_max);
    RUN_TEST(test_fw_string_passes_through);
    RUN_TEST(test_return_value_is_snprintf_style_length);
    RUN_TEST(test_battery_fields_included_when_present);
    RUN_TEST(test_battery_fields_omitted_when_absent);
    RUN_TEST(test_tick_health_fields_included_when_present);
    RUN_TEST(test_tick_health_fields_omitted_when_null);
    RUN_TEST(test_battery_and_tick_health_together);
    RUN_TEST(test_tick_health_truncation_is_reported);
    return UNITY_END();
}
