#pragma once
// P4Hub web UI (P4-007): rack-panel status + config page styled like X32Link,
// served over esp_http_server. Thin glue; the /status JSON is pure
// p4hub_status.c and the config model is pure p4hub_config.c.
#include <stdint.h>
#include "p4hub_config.h"
#ifdef __cplusplus
extern "C" {
#endif
// Start the web server. cfg is the live config: read to render the page, fully
// rewritten + saved + rebooted on POST /save, and patched in place (live-safe
// fields only, no reboot) on POST /live (P4-015). `gen` is a config-generation
// counter bumped on every /live change so the clock task re-primes its grids;
// pass the address of a counter the clock task watches. Both must outlive the
// server.
void p4hub_web_start(P4HubConfig* cfg, volatile uint32_t* gen);
#ifdef __cplusplus
}
#endif
