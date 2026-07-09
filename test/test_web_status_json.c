#include "unity.h"
#include "web_status_json.h"
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// bpm/phase/valid/quantum/fw all show up, valid serializes as a real JSON bool.
void test_valid_reading_includes_all_fields(void) {
    char buf[96];
    web_status_json(buf, sizeof(buf), 120.0f, 1.2500f, true, 4, "2.1.0");
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":120.0,\"phase\":1.2500,\"valid\":true,\"quantum\":4,\"fw\":\"2.1.0\"}", buf);
}

// No phase reading yet (tempo_source_phase()'s -1.0f sentinel) — still
// printed as-is; `valid:false` is what JS actually gates on, per LNK-022.
void test_invalid_reading_passes_phase_through_unspecial_cased(void) {
    char buf[96];
    web_status_json(buf, sizeof(buf), 0.0f, -1.0f, false, 4, "2.1.0");
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":0.0,\"phase\":-1.0000,\"valid\":false,\"quantum\":4,\"fw\":\"2.1.0\"}", buf);
}

// quantum_beats round-trips at both ends of its valid range (1-16, LNK-019).
void test_quantum_min_and_max(void) {
    char buf[96];
    web_status_json(buf, sizeof(buf), 100.0f, 0.0f, true, 1, "2.1.0");
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":100.0,\"phase\":0.0000,\"valid\":true,\"quantum\":1,\"fw\":\"2.1.0\"}", buf);
    web_status_json(buf, sizeof(buf), 100.0f, 15.9999f, true, 16, "2.1.0");
    TEST_ASSERT_EQUAL_STRING("{\"bpm\":100.0,\"phase\":15.9999,\"valid\":true,\"quantum\":16,\"fw\":\"2.1.0\"}", buf);
}

// LNK-038: the fw string is caller-supplied (the pure builder stays
// version-agnostic — glue passes FW_VERSION from fw_version.h).
void test_fw_string_passes_through(void) {
    char buf[96];
    web_status_json(buf, sizeof(buf), 120.0f, 0.0f, true, 4, "9.9.9-rc1");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"fw\":\"9.9.9-rc1\""));
}

// Return value mirrors snprintf's contract — bytes that would've been
// written, so a too-small buffer is detectable by the caller.
void test_return_value_is_snprintf_style_length(void) {
    char buf[96];
    int n = web_status_json(buf, sizeof(buf), 120.0f, 1.25f, true, 4, "2.1.0");
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);

    char tiny[4];
    int n2 = web_status_json(tiny, sizeof(tiny), 120.0f, 1.25f, true, 4, "2.1.0");
    TEST_ASSERT_TRUE(n2 > (int)sizeof(tiny) - 1);  // truncated but length still reported
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_reading_includes_all_fields);
    RUN_TEST(test_invalid_reading_passes_phase_through_unspecial_cased);
    RUN_TEST(test_quantum_min_and_max);
    RUN_TEST(test_fw_string_passes_through);
    RUN_TEST(test_return_value_is_snprintf_style_length);
    return UNITY_END();
}
