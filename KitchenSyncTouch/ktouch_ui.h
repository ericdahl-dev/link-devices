#pragma once
// Pure transport-button hit-test (ESP-016). Touch is transport-ONLY: two big
// buttons, PLAY and STOP, no settings screen (a stray tap must never change a
// setting mid-performance). Screen is the 172x320 LCD in rotation 6. Host-tested
// in test/test_ktouch_ui.c. No Arduino/LovyanGFX dependency.
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KTOUCH_BTN_NONE = 0, KTOUCH_BTN_PLAY, KTOUCH_BTN_STOP } KtouchBtn;

// Which transport button contains screen point (x,y), or KTOUCH_BTN_NONE.
int ktouch_ui_hit(int x, int y);

#ifdef __cplusplus
}
#endif
