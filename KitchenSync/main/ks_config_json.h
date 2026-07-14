#pragma once
// Pure GET /config.json builder (P4-041) -- the read-side counterpart to
// ks_form_resolve()/ks_form_apply() (ks_form.c, the write side). Field names match
// the POST /save and /live form keys 1:1 (see ks_config_set() in ks_config.c),
// so a client never has to maintain a second name mapping for reading vs. writing.
//
// Exists because /status (ks_status.c) is telemetry only -- bpm, peers, launch
// state -- and never carried the device's actual settings. The only place a
// client could previously read "is output 2 enabled, what's its division" was by
// scraping the rendered HTML of GET / (ks_web.cpp's root_handler), which ties a
// client to the web UI's markup instead of a contract. No existing consumer
// (X32Link, the P4 web UI itself) parses this; it's additive, written for the
// iOS companion app (docs/plans/2026-07-14-ios-kitchensync-app-plan.md).
//
// SECURITY: WiFi passwords are NEVER included -- same rule ks_web.cpp's
// build_wifi() already follows for the HTML form (a saved password is never
// echoed back to the browser, only a "keep current" placeholder). Here that's
// `wifi_pass_set`: true if the slot has a saved password, never the password
// itself, so a client can show "configured" without the secret ever crossing
// LAN HTTP in the read direction either.
#include <stddef.h>
#include "ks_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// Formats the full KsConfig as JSON:
//   {"clock_out":bool,"metronome":bool,"metro_accent":bool,"metro_vol":N,
//    "metro_voice":N,"led":bool,"led_bright":N,"led_mode":N,"led_fade":N,
//    "led_beat":"#rrggbb","led_accent":"#rrggbb","follow_beat":bool,
//    "wifi":[{"ssid":"S","pass_set":bool}, ...],
//    "clock":[{"en":bool,"cable":N,"ppqn":N,"phase":N,"swing":N,"follow":bool}, ...]}
// `wifi` has KS_WIFI_SLOTS entries, `clock` has KS_CLOCK_OUTPUTS entries, same
// order as the form's slot/output numbering (slot/output 0 is the unsuffixed
// "wifi_ssid"/"clk0_*" form keys). Returns snprintf()'s return value (same
// truncation-detection convention as ks_status_json()) -- a caller comparing it
// against `len` can tell a truncated buffer from a short one.
int ks_config_json(char* buf, size_t len, const KsConfig* c);

#ifdef __cplusplus
}
#endif
