#pragma once
// Pure /status JSON builder for the P4Hub web UI (P4-007). No ESP-IDF/network
// dependency — just snprintf, host-tested in test/test_p4hub_status.c. Same
// "pull the formatting out of the server glue" pattern as X32Link's
// web_status_json.c.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Formats {"bpm":F,"peers":N,"usb":bool,"tx":N} into buf. `bpm` is the live Link
// session tempo, `peers` the Link peer count, `usb` whether a USB-MIDI device is
// ready, `tx` the running clock-pulse count. Returns snprintf()'s return value so
// the caller can detect truncation.
int p4hub_status_json(char* buf, size_t len, float bpm, int peers, bool usb, uint32_t tx);

#ifdef __cplusplus
}
#endif
