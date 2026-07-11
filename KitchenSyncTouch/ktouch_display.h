#pragma once
// KitchenSync Touch on-device display (ESP-016). Status screen (Inc 1c): live BPM
// + sync state + IP. The transport PLAY/STOP screen + touch input land in Inc2.
void ktouch_display_begin(void);
void ktouch_display_tick(void);
void ktouch_display_set_brightness(int pct);   // 10..100, live (PWM backlight)

// Bench probe: why did a cue leave the screen mid-hold? /status surfaces these.
#include <stdint.h>
uint32_t ktouch_touch_fails(void);   // I2C reads that failed outright
uint32_t ktouch_touch_zeros(void);   // reads that SUCCEEDED but said 0 points while held
uint32_t ktouch_cue_cancels(void);   // cues killed by the sustained-fault path
int      ktouch_cueing(void);        // 1 = CUE panel showing right now
