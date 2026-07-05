#pragma once
// P4Hub web UI (P4-007): rack-panel status page styled like X32Link, served over
// esp_http_server. Thin glue; the /status JSON is the pure p4hub_status.c.
#ifdef __cplusplus
extern "C" {
#endif
void p4hub_web_start(void);
#ifdef __cplusplus
}
#endif
