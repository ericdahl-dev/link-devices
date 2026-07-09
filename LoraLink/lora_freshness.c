#include "lora_freshness.h"

bool lora_freshness_is_stale(uint32_t now_ms, uint32_t last_seen_ms,
                              uint32_t threshold_ms, bool has_received) {
    if (!has_received) return true;
    return (uint32_t)(now_ms - last_seen_ms) >= threshold_ms;
}
