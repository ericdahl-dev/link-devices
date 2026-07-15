// ESP-026 spike: the KitchenSync Touch 1.47" LCD, driven from the ESP-IDF firmware
// instead of the Arduino KitchenSyncTouch sketch. This is the ONE genuinely new piece
// of the Touch -> ESP-IDF convergence (ADR-0009); everything else the Touch needs
// (wifi_link, ks_web, config, transport, the clock engine) already exists here.
//
// Ported from KitchenSyncTouch/ktouch_display.cpp. The panel/touch config and every
// draw call are pure LovyanGFX and carry over unchanged; only the Arduino primitives
// (analogWrite -> ledc, Wire -> i2c_master, pinMode/delay -> gpio/vTaskDelay,
// millis -> esp_timer, WiFi.status -> wifi_link) are replaced.
//
// Compiled into every target, but real only when CONFIG_KS_TOUCH_DISPLAY is set
// (Kconfig: depends on IDF_TARGET_ESP32S3). On the P4 and headless builds it is a
// no-op, so ks_main can call it unconditionally.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the panel + backlight and spawn the render task. Idempotent-safe to call
// once from app_main. No-op unless CONFIG_KS_TOUCH_DISPLAY.
void ks_display_start(void);

// Set backlight brightness (percent). No-op unless CONFIG_KS_TOUCH_DISPLAY.
void ks_display_set_brightness(int pct);

#ifdef __cplusplus
}
#endif
