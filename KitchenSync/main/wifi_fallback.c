#include "wifi_fallback.h"

bool wifi_fallback_should_give_up(int64_t now_us, int64_t connect_start_us) {
    return (now_us - connect_start_us) >= WIFI_FALLBACK_TIMEOUT_US;
}
