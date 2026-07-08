#pragma once
// KitchenSync web UI (P4-007): rack-panel status + config page styled like X32Link,
// served over esp_http_server. Thin glue; the /status JSON is pure
// ks_status.c and the config model is pure ks_config.c.
#include <stdint.h>
#include "ks_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#ifdef __cplusplus
extern "C" {
#endif
// Start the web server. cfg is the live config: read to render the page, fully
// rewritten + saved + rebooted on POST /save, and patched in place (live-safe
// fields only, no reboot) on POST /live (P4-015). `gen` is a config-generation
// counter bumped on every /live change so the clock task re-primes its grids;
// pass the address of a counter the clock task watches. Both must outlive the
// server. cfg_mutex (ARC-016) guards the /live patch against the clock task's read so
// the task never sees a torn multi-field update; both hold it only for the copy.
void ks_web_start(KsConfig* cfg, volatile uint32_t* gen, SemaphoreHandle_t cfg_mutex);
#ifdef __cplusplus
}
#endif
