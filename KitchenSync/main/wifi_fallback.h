// Pure STA-connect-timeout decision for KitchenSync's WiFi glue (wifi_link.c).
// No ESP-IDF dependency — host-testable in test/test_wifi_fallback.c.
//
// Parity with X32Link's wifi_try_connect(): give the configured network a fixed
// budget to associate; once it's blown, the glue layer gives up on STA and parks
// in the "KitchenSync-Setup" AP config fallback instead of retrying forever (which
// is what the hub firmware did before this — see tasks/P4-027.md).
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Same budget as X32Link's wifi_try_connect() 30s deadline, in microseconds
// (wifi_link.c's clock is esp_timer_get_time(), not millis()).
#define WIFI_FALLBACK_TIMEOUT_US (30LL * 1000000LL)

// True once `now_us - connect_start_us` has reached the timeout budget. Boundary
// is inclusive (>=), matching X32Link's `millis() - start > 30000` give-up-
// once-fully-elapsed semantics.
bool wifi_fallback_should_give_up(int64_t now_us, int64_t connect_start_us);

#ifdef __cplusplus
}
#endif
