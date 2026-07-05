#pragma once
// P4Hub web UI (P4-007): rack-panel status + config page styled like X32Link,
// served over esp_http_server. Thin glue; the /status JSON is pure
// p4hub_status.c and the config model is pure p4hub_config.c.
#include "p4hub_config.h"
#ifdef __cplusplus
extern "C" {
#endif
// Start the web server. cfg is the live config (read to render the page, updated
// + saved + rebooted on POST /save). Must outlive the server.
void p4hub_web_start(P4HubConfig* cfg);
#ifdef __cplusplus
}
#endif
