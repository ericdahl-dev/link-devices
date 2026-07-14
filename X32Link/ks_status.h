#pragma once
// Pure /status JSON builder for the KitchenSync web UI (P4-007). No ESP-IDF/network
// dependency — just snprintf, host-tested in test/test_ks_status.c. Same
// "pull the formatting out of the server glue" pattern as X32Link's
// web_status_json.c.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "web_status_json.h"
#include "link_measurement.h"   /* P4-038: LinkPhaseHealth — the origin-step gauge */   /* P4-038: WebTickHealth — the SAME struct and the same
                                * JSON keys X32Link publishes (ARC-024). One shape across
                                * the fleet, so one script audits every device. */
#ifdef __cplusplus
extern "C" {
#endif

// Formats {"bpm":F,"min":F,"peers":N,"usb":bool,"tx":N,"fw":"S","follow_enabled":bool,
// "follow_bpm":F,"follow_confidence":F,"follow_valid":bool} into buf. `bpm` is the
// live Link session tempo, `min` the detected MIDI-clock-IN tempo (0 when no clock
// in, P4-011), `peers` the Link peer count, `usb` whether a USB-MIDI device is
// ready, `tx` the running clock-pulse count, `fw` the firmware version string
// (LNK-038). `follow_enabled` is the mic tempo-follow feature's config toggle
// (P4-020) -- kept separate from `follow_valid` because both are false when the
// feature is off AND when it's on-but-not-yet-confident; the web UI needs to tell
// those two states apart ("off" vs "listening..."), so it must branch on
// `follow_enabled` first, not infer it from `follow_valid`. `follow_bpm`/
// `follow_confidence`/`follow_valid` are 0/0/false whenever `follow_enabled` is
// false. Returns snprintf()'s return value so the caller can detect truncation.
// `launch` is the per-output quantized-launch state (ESP-011): 0 stopped,
// 1 armed (waiting for the bar line), 2 running. Emitted as "launch":[a,b,c,d]
// so the UI can render "starting on next bar..." rather than a dead button.
// `playing` is the session's real transport state and `link_owns` is true once a
// Link peer has published StartStopState (ks_tick arbitration): when link_owns is
// true the manual PLAY/STOP buttons are ignored, so the UI greys them and shows
// the session's `playing` state instead of the (frozen) manual launch state.
// `tick` is the 1 ms clock task's health (P4-038), emitted as the same
// "drop"/"burst"/"gap"/"work"/"over"/"core"/"w_beats"/"w_clock" block X32Link
// publishes (ARC-024), so one fleet script reads every device. Pass NULL to omit the
// block entirely -- never publish a row of zeroes, which would read as "measured, all
// clean" when the truth is "not measured".
//
// The numbers are LIFETIME, not windowed. That is the whole point: KitchenSync already
// had this probe and threw it away into a once-a-second log, so a 766 ms clock stall
// (P4-038) left no trace anywhere unless someone happened to have serial attached at
// that moment. A worst-case that scrolled past is not a measurement.
//
// ESP-029: this builder is SHARED. It lives in X32Link/ (the shared-module home) and is
// symlinked into KitchenSyncTouch/, exactly like the 46 other modules the two products
// already share. Both firmwares emit /status from THIS function, so the wire shape is
// identical BY CONSTRUCTION rather than by discipline.
//
// That is the whole point. Before this, ktouch_web.cpp hand-rolled its own snprintf and
// ks_status.c had another, and they drifted: the same nine tick-health fields, spelled
// `wbeats`/`wclock` on one device and `w_beats`/`w_clock` on the other. Nobody chooses
// that. Worse, the same "/status lies about the clock" bug had to be fixed TWICE --
// P4-039 did it for the P4 and, per ESP-028, "the Touch never got it", which is what
// cost 138 seconds of silent DIN clock. One implementation, one host-test suite, no
// drift.
//
// `launch` / `launch_count`: the array LENGTH is the device's REAL output count -- 4 on
// KitchenSync, 1 on the Touch. NEVER pad. A client renders a card per element, so
// padding a one-output device out to four would draw three dead outputs.
//
// `clk` / `pulses` (ESP-028; optional -- pass NULL to omit): what the WRITER is doing
// ("locked" / "free" / "silent") and the lifetime 0xF8 count. Together they make "is the
// wire actually alive?" answerable by polling instead of with a logic analyzer. NULL
// means ABSENT, never a default -- a device that does not measure its writer must not
// claim `clk:"locked"`, which is precisely the lie ESP-028 exists to prevent.
//
// `extra` (optional -- NULL to omit): device-specific diagnostics, appended INSIDE the
// object. A bare fragment, no braces and no leading comma. The Touch's own page reads
// `cue`, `btn`, `beats` and friends; they ride here so the SHARED keys stay byte-identical
// across the fleet while each device can still say more. A client ignores keys it does not
// know, which costs nothing.
int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid, const int* launch, int launch_count, bool playing, bool link_owns,
                   const WebTickHealth* tick, const LinkPhaseHealth* phase,
                   const char* clk, const uint32_t* pulses, const char* extra);

#ifdef __cplusplus
}
#endif
