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

// ESP-030: what this board actually HAS.
//
// A device must not report hardware it does not have. Emitting `led:false` on a
// board with no strip is the same class of lie as ESP-028's `sync:1` over a wire
// that had been dead for 138 seconds -- and a client would dutifully draw an LED
// section for a device that cannot light anything. Absent hardware is ABSENT from
// the document, not reported false.
//
// Capabilities are a property of the BUILD, not of the product name. "The Touch has
// no LED" is wrong -- the truth is "this Touch has no strip WIRED UP YET". Attach
// one, flip the board flag, and /config.json starts emitting led_*, and a client's
// settings screen grows an LED section with no client change at all. So this comes
// from the board config, never from a hardcoded product identity.
typedef struct {
    bool metronome;    // a speaker is fitted (ES8311 / buzzer)
    bool led;          // a WS2812 strip is wired
    bool follow_beat;  // a mic is fitted
    int  outputs;      // clock outputs FITTED -- the `clock` array's length. Never padded.

    /* ESP-035: how many wifi credentials this build can actually STORE.
     *
     * `wifi` used to be hardcoded to KS_WIFI_SLOTS, which is a lie on any board that
     * holds fewer. The X32Link holds ONE. Advertise three and a client renders three:
     * the user saves a second network, the device's /save reads only `wifi_ssid` and
     * throws it away, and the client cheerfully reports success.
     *
     * Not hypothetical -- that is the bug that cost the user their second network on the
     * Touch (ESP-030 pt3), here reachable from the READ side. The array's LENGTH is the
     * truth, exactly as it already is for `clock`. */
    int  wifi_slots;
} KsCaps;

// Formats KsConfig as JSON, emitting ONLY what `caps` says the board has:
//   {"clock_out":bool,
//    ["metronome":bool,"metro_accent":bool,"metro_vol":N,"metro_voice":N,]   // iff caps.metronome
//    ["led":bool,"led_bright":N,"led_mode":N,"led_fade":N,
//     "led_beat":"#rrggbb","led_accent":"#rrggbb",]                          // iff caps.led
//    ["follow_beat":bool,]                                                   // iff caps.follow_beat
//    "wifi":[{"ssid":"S","pass_set":bool}, ...],
//    "clock":[{"en":bool,"cable":N,"ppqn":N,"phase":N,"swing":N,"follow":bool}, ...]}
//
// `wifi` has caps.wifi_slots entries and `clock` has caps.outputs entries -- the FITTED
// counts, never padded, because a client renders a row/card per element and would
// otherwise draw slots and outputs the device cannot honour. Same order as the form's
// slot/output numbering (slot 0 is the unsuffixed "wifi_ssid"/"clk0_*" keys).
//
// Returns snprintf()'s return value (same truncation-detection convention as
// ks_status_json()) -- a caller comparing it against `len` can tell a truncated
// buffer from a short one.
int ks_config_json(char* buf, size_t len, const KsConfig* c, const KsCaps* caps);

#ifdef __cplusplus
}
#endif
