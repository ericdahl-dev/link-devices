#pragma once
// Pure /status JSON builder for the KitchenSync web UI (P4-007). No ESP-IDF/network
// dependency — just snprintf, host-tested in test/test_ks_status.c. Same
// "pull the formatting out of the server glue" pattern as X32Link's
// web_status_json.c.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
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
int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid);

#ifdef __cplusplus
}
#endif
