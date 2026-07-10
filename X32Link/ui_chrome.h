#pragma once
// ARC-017: the web chrome both firmwares' config UIs share — the rack-panel look
// and the client-side plumbing. Pure C: no Arduino, no ESP-IDF, just snprintf.
// Compiled into KitchenSync by relative path (X32LINK_DIR in its CMakeLists),
// same as clock_ticker.c and the rest of the shared pure C. Host-tested in
// test/test_ui_chrome.c.
//
// The *forms* stay per-firmware: X32Link's is tempo source / mixer model / FX
// slot, KitchenSync's is 4x clock output + metronome + LED strip. There is no
// shared config page to extract, and pretending otherwise would produce a
// parameterized monster. This owns only what was genuinely duplicated.
//
// Cascade order matters: emit ui_chrome_css() FIRST, then the page's own <style>
// block. Several page rules deliberately extend a shared one (X32Link re-adds
// `.unit{animation:rise}`, KitchenSync adds `.fld{margin-top}`), and they only
// win by coming second.
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The panel stylesheet: contents of a <style> block, no tags. Static storage,
// never NULL. One definition of --mut, .unit, .screw, .scr, .sw, @keyframes
// breathe. Rules that only one firmware uses (X32Link's .seg/.slots/rise entry
// animations, KitchenSync's .sect/.stp/desktop media query) stay in that
// firmware's own block.
const char* ui_chrome_css(void);

// The shared client-side JS: contents of a <script> block, no tags. Static
// storage, never NULL. Provides setBeat / flashBeat / showBpm / postLive and a
// poll() that fetches /status, calls showBpm, then hands the parsed object to a
// page-supplied `onStatus(d)` hook. Each firmware appends its own <script> with
// that hook and any page-specific wiring.
//
// Two contracts the pages rely on:
//   - the page must define `function onStatus(d)` before poll() first runs;
//   - a page that drives the beat dot itself (X32Link's LNK-022 phase lock) sets
//     `chromePhaseLocked = true`, which makes showBpm() stop calling setBeat().
// It is a hook, not a `product` flag. Resist adding one.
const char* ui_chrome_js(void);

// Result page shown after Write & Reboot / OTA. `reboot` appends the poller that
// waits for the device to come back and then redirects to '/', so the browser is
// not left stranded on the result page. Replaces both send_result() bodies.
//
// Returns what snprintf returns: the length the page WOULD have needed. A value
// >= len means it was truncated. Same contract as ks_status_json().
int ui_result_page(char* buf, size_t len,
                   const char* title, const char* body, bool reboot);

// GET /update page. `product` is the title / heading prefix ("X32&middot;SYNC",
// "KitchenSync"); `fw` and `build` are FW_VERSION / FW_BUILD, passed in so this
// stays pure. `multipart` picks the form: true -> <form enctype=multipart> for
// Arduino's Update.h, false -> the fetch(body:file) uploader for esp_ota_ops.
//
// Returns what snprintf returns (see above).
int ui_update_page(char* buf, size_t len, const char* product,
                   const char* fw, const char* build, bool multipart);

#ifdef __cplusplus
}
#endif
