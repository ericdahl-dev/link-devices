#include "config.h"        // board -> HAS_TOUCH_DISPLAY; must precede the guard
#include "ktouch_display.h"

// Compiles only for the touch-LCD board (keeps LovyanGFX out of screenless builds).
#ifdef HAS_TOUCH_DISPLAY

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <math.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "app_config.h"
#include "tempo_source.h"
#include "axs5106l.h"
#include "ktouch_ui.h"
#include "ktouch_transport.h"

extern AppConfig g_config;
extern char g_ks_host[];

// Panel: JD9853 via Panel_ST7789 (validated on glass, LNK-014). Same config as
// X32Link/touch_display.cpp. 172x320 visible in 240x320 RAM.
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
public:
    LGFX() {
        { auto c = _bus.config();
          c.spi_host = SPI2_HOST; c.spi_mode = 0;
          c.freq_write = 40000000; c.freq_read = 16000000;
          c.pin_sclk = 38; c.pin_mosi = 39; c.pin_miso = -1; c.pin_dc = 45;
          c.dma_channel = SPI_DMA_CH_AUTO;
          _bus.config(c); _panel.setBus(&_bus); }
        { auto c = _panel.config();
          c.pin_cs = 21; c.pin_rst = 40; c.pin_busy = -1;
          c.memory_width = 240; c.memory_height = 320;
          c.panel_width = 172;  c.panel_height = 320;
          c.offset_x = 34; c.offset_y = 0;
          c.invert = false; c.rgb_order = false;
          _panel.config(c); }
        setPanel(&_panel);
    }
};

#define LCD_BL   46
#define TP_SDA   42
#define TP_SCL   41
#define TP_RST   47
#define SCR_W    320
#define SCR_H    172
#define WHEEL_CX 286
#define WHEEL_CY 36
#define WHEEL_R  28
// Transport buttons (must match ktouch_ui.c rects: PLAY 8..156, STOP 164..312, y 88..168).
#define BTN_Y0   88
#define BTN_Y1   168

static LGFX     s_lcd;
static float    s_shown_bpm = -1.0f;
static int      s_shown_sync = -2;
static int      s_prev_mx = -1, s_prev_my = -1;
static bool     s_touch_down = false;
static uint32_t s_last_text = 0;

static void backlight_on(void) { pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH); }

// I2C read + pure parse of the AXS5106L (0x63). Same as X32Link's glue; axs5106l
// itself is pure (no Wire), so the read lives here.
static bool read_touch(axs_touch_t *t) {
    Wire.beginTransmission(AXS5106L_I2C_ADDR);
    Wire.write(AXS5106L_TOUCH_REG);
    if (Wire.endTransmission(true) != 0) return false;
    uint8_t buf[AXS5106L_REPORT_LEN];
    if (Wire.requestFrom((int)AXS5106L_I2C_ADDR, (int)AXS5106L_REPORT_LEN) != AXS5106L_REPORT_LEN)
        return false;
    for (int i = 0; i < AXS5106L_REPORT_LEN; i++) buf[i] = Wire.read();
    return axs5106l_parse(buf, sizeof(buf), t) == 0;
}

static void draw_button(int which, bool pressed) {
    int x0 = which == KTOUCH_BTN_PLAY ? 8 : 164;
    int x1 = which == KTOUCH_BTN_PLAY ? 156 : 312;
    uint32_t on = which == KTOUCH_BTN_PLAY ? 0x2ea00cu : 0xa02a0cu;   // green / red
    uint32_t bg = pressed ? on : 0x101317u;
    s_lcd.fillRoundRect(x0, BTN_Y0, x1 - x0, BTN_Y1 - BTN_Y0, 10, bg);
    s_lcd.drawRoundRect(x0, BTN_Y0, x1 - x0, BTN_Y1 - BTN_Y0, 10, on);
    s_lcd.setTextColor(pressed ? TFT_BLACK : on, bg);
    s_lcd.setTextSize(4);
    s_lcd.setCursor(x0 + 22, BTN_Y0 + 24);
    s_lcd.print(which == KTOUCH_BTN_PLAY ? "PLAY" : "STOP");
}

// Static frame: header, wheel outline, buttons. Redrawn on a sync-state change.
static void draw_frame(void) {
    s_lcd.fillScreen(TFT_BLACK);
    s_lcd.setTextColor(0x838d95u, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(8, 4); s_lcd.println("KITCHENSYNC TOUCH");
    s_lcd.drawCircle(WHEEL_CX, WHEEL_CY, WHEEL_R, TFT_DARKGREEN);
    draw_button(KTOUCH_BTN_PLAY, false);
    draw_button(KTOUCH_BTN_STOP, false);
    s_prev_mx = s_prev_my = -1;
}

void ktouch_display_begin(void) {
    s_lcd.init();
    s_lcd.setRotation(7);   // landscape 320x172
    backlight_on();
    draw_frame();
    // AXS5106L touch controller up (I2C 0x63).
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);  delay(20);
    digitalWrite(TP_RST, HIGH); delay(80);
    Wire.begin(TP_SDA, TP_SCL); Wire.setClock(400000);
}

// Map raw AXS5106L point to landscape screen space. GUESS for rotation 7 -- the
// serial log below prints raw+mapped so it can be calibrated on glass, then this
// transform is locked.
static void map_touch(const axs_point_t* p, int* sx, int* sy) {
    *sx = p->y;            // raw Y -> screen X (0..319)
    *sy = 171 - p->x;      // raw X -> screen Y (0..171), flipped
}

void ktouch_display_tick(void) {
    float bpm  = tempo_source_bpm();
    int   sync = tempo_source_phase_valid() ? 1 : (tempo_source_active() ? 0 : -1);
    uint32_t now = millis();

    // ---- touch dispatch (transport only) ----
    axs_touch_t t;
    if (read_touch(&t)) {
        bool touched = t.points_len > 0;
        if (touched && !s_touch_down) {
            s_touch_down = true;
            int sx, sy; map_touch(&t.points[0], &sx, &sy);
            int btn = ktouch_ui_hit(sx, sy);
            Serial.printf("[KSTouch] tap raw(%d,%d) -> scr(%d,%d) -> btn=%d\n",
                          t.points[0].x, t.points[0].y, sx, sy, btn);
            if (btn == KTOUCH_BTN_PLAY) { ktouch_transport_post(TL_INTENT_PLAY); draw_button(KTOUCH_BTN_PLAY, true); }
            else if (btn == KTOUCH_BTN_STOP) { ktouch_transport_post(TL_INTENT_STOP); draw_button(KTOUCH_BTN_STOP, true); }
        } else if (!touched && s_touch_down) {
            s_touch_down = false;                       // release: un-highlight
            draw_button(KTOUCH_BTN_PLAY, false);
            draw_button(KTOUCH_BTN_STOP, false);
        }
    }

    // ---- status text (throttled, redraw on change) ----
    if (now - s_last_text >= 200 &&
        (fabsf(bpm - s_shown_bpm) >= 0.05f || sync != s_shown_sync)) {
        s_last_text = now;
        bool state_changed = sync != s_shown_sync;
        s_shown_bpm = bpm; s_shown_sync = sync;

        s_lcd.fillRect(0, 16, 200, 50, TFT_BLACK);   // BPM area (top strip)
        s_lcd.setTextColor(0xB6FF36u, TFT_BLACK); s_lcd.setTextSize(5);
        s_lcd.setCursor(8, 18);
        if (bpm > 0.0f) s_lcd.printf("%3.0f", bpm); else s_lcd.print("---");
        s_lcd.setTextColor(0x6f8a4du, TFT_BLACK); s_lcd.setTextSize(1);
        s_lcd.setCursor(100, 52); s_lcd.println("BPM");

        s_lcd.fillRect(0, 64, 260, 20, TFT_BLACK);
        s_lcd.setTextColor(sync == 1 ? 0xB6FF36u : 0xFF9D3Bu, TFT_BLACK); s_lcd.setTextSize(1);
        s_lcd.setCursor(150, 20);
        s_lcd.println(sync == 1 ? "SYNCED" : sync == 0 ? "linking" : "no link");
        s_lcd.setTextColor(TFT_WHITE, TFT_BLACK); s_lcd.setTextSize(1);
        s_lcd.setCursor(8, 70);
        if (WiFi.status() == WL_CONNECTED) { s_lcd.print(g_ks_host); s_lcd.println(".local"); }
        else s_lcd.println("no wifi");

        if (state_changed) s_prev_mx = -1;   // force wheel repaint
    }

    // ---- sync wheel marker ----
    float phase = tempo_source_phase((float)g_config.quantum_beats);
    if (phase >= 0.0f && g_config.quantum_beats > 0) {
        float ang = fmodf(phase / (float)g_config.quantum_beats, 1.0f) * 360.0f;
        float rad = (ang - 90.0f) * 0.01745329f;
        int mx = WHEEL_CX + (int)(cosf(rad) * (WHEEL_R - 6));
        int my = WHEEL_CY + (int)(sinf(rad) * (WHEEL_R - 6));
        if (mx != s_prev_mx || my != s_prev_my) {
            if (s_prev_mx >= 0) s_lcd.fillCircle(s_prev_mx, s_prev_my, 5, TFT_BLACK);
            s_lcd.drawCircle(WHEEL_CX, WHEEL_CY, WHEEL_R, TFT_DARKGREEN);
            s_lcd.fillCircle(mx, my, 4, TFT_GREEN);
            s_prev_mx = mx; s_prev_my = my;
        }
    }
}

#else  // no touch display on this board -> no-ops
void ktouch_display_begin(void) {}
void ktouch_display_tick(void) {}
#endif
