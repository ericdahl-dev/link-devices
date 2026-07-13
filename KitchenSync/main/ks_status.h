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
int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid, const int launch[4], bool playing, bool link_owns,
                   const WebTickHealth* tick, const LinkPhaseHealth* phase);

#ifdef __cplusplus
}
#endif
