#pragma once
// KitchenSync Touch on-device display (ESP-016). Status screen (Inc 1c): live BPM
// + sync state + IP. The transport PLAY/STOP screen + touch input land in Inc2.
void ktouch_display_begin(void);
void ktouch_display_tick(void);
void ktouch_display_set_brightness(int pct);   // 10..100, live (PWM backlight)
