// Host tests for the shared web chrome (ARC-017).
#include "unity.h"
#include "ui_chrome.h"
#include <string.h>

static char buf[4096];
void setUp(void)    { memset(buf, 0, sizeof(buf)); }
void tearDown(void) {}

static bool has(const char* hay, const char* needle) { return strstr(hay, needle) != NULL; }

/* ---- static blocks ------------------------------------------------------- */

// Never NULL, and no framework leaked in. These strings are pasted into a page
// between <style>/<script> tags, so a stray tag would close the block early.
void test_css_is_a_style_body_not_a_document(void) {
    const char* css = ui_chrome_css();
    TEST_ASSERT_NOT_NULL(css);
    TEST_ASSERT_FALSE(has(css, "<style"));
    TEST_ASSERT_FALSE(has(css, "</style"));
    TEST_ASSERT_TRUE(has(css, ":root{"));
    TEST_ASSERT_TRUE(has(css, "@keyframes breathe"));
}

void test_js_is_a_script_body_not_a_document(void) {
    const char* js = ui_chrome_js();
    TEST_ASSERT_NOT_NULL(js);
    TEST_ASSERT_FALSE(has(js, "<script"));
    TEST_ASSERT_FALSE(has(js, "</script"));
    TEST_ASSERT_TRUE(has(js, "function setBeat"));
    TEST_ASSERT_TRUE(has(js, "function showBpm"));
    TEST_ASSERT_TRUE(has(js, "function postLive"));
    TEST_ASSERT_TRUE(has(js, "function poll"));
}

// The whole point of the hook: poll() must hand /status to the page, not switch
// on a product name. A `product` flag creeping in here is the failure mode.
void test_js_calls_the_page_supplied_onStatus_hook(void) {
    const char* js = ui_chrome_js();
    TEST_ASSERT_TRUE(has(js, "onStatus(d)"));
    TEST_ASSERT_TRUE(has(js, "chromePhaseLocked"));
}

// The drift this ticket exists to end. One value each, and it is the resolved one.
void test_css_resolves_the_drifted_values_once(void) {
    const char* css = ui_chrome_css();
    TEST_ASSERT_TRUE(has(css, "--mut:#838d95"));    // the P4's lighter value won
    TEST_ASSERT_FALSE(has(css, "#717a82"));         // ...and the S3's is gone
    TEST_ASSERT_TRUE(has(css, "content:\"Off\""));  // no leading space
    TEST_ASSERT_FALSE(has(css, "content:\" Off\""));
}

// Per-firmware rules must NOT be here — extracting them is what turns a shared
// chrome into a parameterized monster.
void test_css_excludes_per_firmware_rules(void) {
    const char* css = ui_chrome_css();
    TEST_ASSERT_FALSE(has(css, ".seg"));           // X32Link's segmented control
    TEST_ASSERT_FALSE(has(css, ".slot"));          // X32Link's FX slot grid
    TEST_ASSERT_FALSE(has(css, "@keyframes rise")); // X32Link's entry animation
    TEST_ASSERT_FALSE(has(css, ".sect"));          // KitchenSync's collapsible group
    TEST_ASSERT_FALSE(has(css, ".stp"));           // KitchenSync's nudge stepper
    TEST_ASSERT_FALSE(has(css, "min-width:760px")); // KitchenSync's desktop block
}

/* ---- result page --------------------------------------------------------- */

void test_result_page_interpolates_title_and_body(void) {
    int n = ui_result_page(buf, sizeof(buf), "Saved &mdash; Restarting", "Reconnect if creds changed.", false);
    TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "Saved &mdash; Restarting"));
    TEST_ASSERT_TRUE(has(buf, "Reconnect if creds changed."));
}

// reboot=true is the difference between "the browser goes home by itself" and
// "the user stares at a dead result page".
void test_result_page_reboot_flag_selects_the_poller(void) {
    ui_result_page(buf, sizeof(buf), "T", "B", true);
    TEST_ASSERT_TRUE(has(buf, "returning to config"));
    TEST_ASSERT_TRUE(has(buf, "location.href='/'"));

    ui_result_page(buf, sizeof(buf), "T", "B", false);
    TEST_ASSERT_FALSE(has(buf, "returning to config"));
    TEST_ASSERT_FALSE(has(buf, "location.href='/'"));
}

// The %% in the border-radius must survive as a literal percent, not eat an arg.
void test_result_page_emits_literal_percent(void) {
    ui_result_page(buf, sizeof(buf), "T", "B", false);
    TEST_ASSERT_TRUE(has(buf, "border-radius:50%;"));
}

// snprintf contract: the return is what it WOULD have needed, so a caller can
// detect truncation. Same as ks_status_json().
void test_result_page_reports_truncation(void) {
    char small[32];
    int n = ui_result_page(small, sizeof(small), "Title", "Body", true);
    TEST_ASSERT_TRUE(n >= (int)sizeof(small));   // "would have needed" > buffer
    TEST_ASSERT_EQUAL_INT(0, small[sizeof(small) - 1]);  // still NUL-terminated
}

/* ---- update page --------------------------------------------------------- */

void test_update_page_interpolates_product_and_version(void) {
    int n = ui_update_page(buf, sizeof(buf), "KitchenSync", "2.2.0", "Jul 10 2026", false);
    TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(buf));
    TEST_ASSERT_TRUE(has(buf, "<title>KitchenSync &middot; Firmware Update</title>"));
    TEST_ASSERT_TRUE(has(buf, "Running FW 2.2.0 &middot; built Jul 10 2026"));
}

// ESP-020: the page must SAY that updating stops the clock. An OTA writes ~1 MB to
// flash, and a flash write suspends the cache and freezes BOTH cores regardless of task
// priority -- so the 1 ms MIDI writer cannot run for the duration. This is not a
// borderline timing effect to be measured against the jitter floor; it is seconds
// against a 1 ms tick. The device WILL stop clocking mid-update.
//
// So the page tells the truth instead of letting a user find out during a set. All three
// firmwares share this page, and all three have exactly the same problem.
void test_update_page_warns_that_the_clock_stops(void) {
    ui_update_page(buf, sizeof(buf), "KitchenSync Touch", "2.2.0", "b", true);
    TEST_ASSERT_TRUE(has(buf, "stops the clock"));
    TEST_ASSERT_TRUE(has(buf, "Do not update mid-set"));
}

// The two OTA transports need different forms; that is the one thing this builder
// switches on, because the markup differs, not the look.
void test_update_page_multipart_selects_the_arduino_form(void) {
    ui_update_page(buf, sizeof(buf), "X32&middot;SYNC", "1.0", "b", true);
    TEST_ASSERT_TRUE(has(buf, "enctype=\"multipart/form-data\""));
    TEST_ASSERT_TRUE(has(buf, "name=\"update\""));
    TEST_ASSERT_FALSE(has(buf, "fetch('/update'"));   // no JS uploader
}

void test_update_page_non_multipart_selects_the_fetch_uploader(void) {
    ui_update_page(buf, sizeof(buf), "KitchenSync", "1.0", "b", false);
    TEST_ASSERT_FALSE(has(buf, "multipart/form-data"));
    TEST_ASSERT_TRUE(has(buf, "fetch('/update'"));
    TEST_ASSERT_TRUE(has(buf, "id=\"st\""));          // the progress line
}

void test_update_page_emits_literal_percents(void) {
    ui_update_page(buf, sizeof(buf), "P", "1.0", "b", false);
    TEST_ASSERT_TRUE(has(buf, "width:100%;"));
    TEST_ASSERT_TRUE(has(buf, "max-width:380px"));
}

void test_update_page_reports_truncation(void) {
    char small[48];
    int n = ui_update_page(small, sizeof(small), "P", "1.0", "b", true);
    TEST_ASSERT_TRUE(n >= (int)sizeof(small));
    TEST_ASSERT_EQUAL_INT(0, small[sizeof(small) - 1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_css_is_a_style_body_not_a_document);
    RUN_TEST(test_js_is_a_script_body_not_a_document);
    RUN_TEST(test_js_calls_the_page_supplied_onStatus_hook);
    RUN_TEST(test_css_resolves_the_drifted_values_once);
    RUN_TEST(test_css_excludes_per_firmware_rules);
    RUN_TEST(test_result_page_interpolates_title_and_body);
    RUN_TEST(test_result_page_reboot_flag_selects_the_poller);
    RUN_TEST(test_result_page_emits_literal_percent);
    RUN_TEST(test_result_page_reports_truncation);
    RUN_TEST(test_update_page_interpolates_product_and_version);
    RUN_TEST(test_update_page_warns_that_the_clock_stops);
    RUN_TEST(test_update_page_multipart_selects_the_arduino_form);
    RUN_TEST(test_update_page_non_multipart_selects_the_fetch_uploader);
    RUN_TEST(test_update_page_emits_literal_percents);
    RUN_TEST(test_update_page_reports_truncation);
    return UNITY_END();
}
