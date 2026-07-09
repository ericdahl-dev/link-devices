#pragma once
// Pure staleness check for the LoraLink receiver's last-seen BPM packet.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True if the last received packet is older than `threshold_ms`, or if
// `has_received` is false (no packet seen yet since boot). `now_ms` /
// `last_seen_ms` are millis()-style timestamps; unsigned subtraction makes
// this wraparound-safe across the ~49.7-day millis() rollover, same trick
// wifi_down_blink_due() relies on (see X32Link/wifi_down_blink.c).
bool lora_freshness_is_stale(uint32_t now_ms, uint32_t last_seen_ms,
                              uint32_t threshold_ms, bool has_received);

#ifdef __cplusplus
}
#endif
