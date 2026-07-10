// Pure transport-button hit-test — see ktouch_ui.h (ESP-016).
#include "ktouch_ui.h"

// Two big stacked buttons in the lower half of the 172x320 screen (BPM sits
// above). Top-left inclusive, bottom-right exclusive.
typedef struct { int x0, y0, x1, y1; } rect_t;
static const rect_t PLAY_BTN = {   8, 190, 164, 250 };
static const rect_t STOP_BTN = {   8, 258, 164, 312 };

static int in_rect(const rect_t* r, int x, int y) {
    return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1;
}

int ktouch_ui_hit(int x, int y) {
    if (in_rect(&PLAY_BTN, x, y)) return KTOUCH_BTN_PLAY;
    if (in_rect(&STOP_BTN, x, y)) return KTOUCH_BTN_STOP;
    return KTOUCH_BTN_NONE;
}
