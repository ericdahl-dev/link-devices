#pragma once
// KitchenSync Touch web config (ESP-016, Inc3). ALL settings live here, not on the
// touch screen (touch is transport-only): WiFi, quantum, clock/transport enable,
// and the touch-vs-release trigger mode. Reuses the shared ui_chrome look.
void ktouch_web_begin(void);
void ktouch_web_tick(void);   // pump the server (call from loop)
