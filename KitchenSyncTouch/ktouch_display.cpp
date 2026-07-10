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
#include "transport_launch.h"

extern AppConfig g_config;
extern char g_ks_host[];
extern bool g_ap_mode;   // true = SoftAP setup: show how to connect, not a name

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
#define WHEEL_CX 292
#define WHEEL_CY 30
#define WHEEL_R  24
#define TOGGLE_Y 66    // full-width transport toggle fills y 66..171

static LGFX     s_lcd;
static float    s_shown_bpm = -1.0f;
static int      s_shown_sync = -2;
static int      s_shown_state = -1;
static int      s_prev_mx = -1, s_prev_my = -1;
static bool     s_touch_down = false;

static void backlight_on(void) { pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH); }

// I2C read + pure parse of the AXS5106L (0x63). axs5106l itself is pure (no Wire).
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

// The whole lower area is one transport toggle; colour + label follow the state.
static void draw_toggle(int state) {
    uint32_t bg; const char* big; const char* hint;
    if (state == TL_RUNNING)      { bg = 0x1f7a0eu; big = "PLAYING";  hint = "tap to STOP"; }
    else if (state == TL_ARMED)   { bg = 0xb0701au; big = "ARMING";   hint = "starts next bar"; }
    else                          { bg = 0x7a1810u; big = "STOPPED";  hint = "tap to PLAY"; }
    s_lcd.fillRect(0, TOGGLE_Y, SCR_W, SCR_H - TOGGLE_Y, bg);
    s_lcd.setTextColor(TFT_WHITE, bg);
    s_lcd.setTextSize(5); s_lcd.setCursor(16, TOGGLE_Y + 14); s_lcd.print(big);
    s_lcd.setTextSize(2); s_lcd.setCursor(16, TOGGLE_Y + 66); s_lcd.print(hint);
}

// Top strip: header + wheel outline. Redrawn on a sync-state change.
static void draw_frame(void) {
    s_lcd.fillScreen(TFT_BLACK);
    s_lcd.setTextColor(0x838d95u, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(8, 4); s_lcd.println("KITCHENSYNC TOUCH");
    s_lcd.drawCircle(WHEEL_CX, WHEEL_CY, WHEEL_R, TFT_DARKGREEN);
    s_prev_mx = s_prev_my = -1;
}

void ktouch_display_begin(void) {
    s_lcd.init();
    s_lcd.setRotation(7);   // landscape 320x172
    backlight_on();
    draw_frame();
    draw_toggle(TL_STOPPED); s_shown_state = TL_STOPPED;
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);  delay(20);
    digitalWrite(TP_RST, HIGH); delay(80);
    Wire.begin(TP_SDA, TP_SCL); Wire.setClock(400000);
}

void ktouch_display_tick(void) {
    float bpm  = tempo_source_bpm();
    int   sync = tempo_source_phase_valid() ? 1 : (tempo_source_active() ? 0 : -1);
    int   state = ktouch_transport_state();
    uint32_t now = millis();

    // ---- transport toggle: ANY touch flips it (no buttons -> no mis-hit) ----
    // Fire on touch-down (digital-DJ) or on release (turntable-DJ: hold, release to
    // let the beat play). Which edge is a web setting (g_config.play_on_release).
    axs_touch_t t;
    if (read_touch(&t)) {
        bool touched = t.points_len > 0;
        if (touched && !s_touch_down) {
            s_touch_down = true;
            if (!g_config.play_on_release) ktouch_transport_post(ktouch_toggle_intent(state));
        } else if (!touched && s_touch_down) {
            s_touch_down = false;
            if (g_config.play_on_release) ktouch_transport_post(ktouch_toggle_intent(state));
        }
    }

    // ---- toggle zone repaint on state change ----
    if (state != s_shown_state) { draw_toggle(state); s_shown_state = state; }

    // ---- status text (top strip): throttled, redraw on change ----
    static uint32_t s_last_text = 0;
    if (now - s_last_text >= 200 &&
        (fabsf(bpm - s_shown_bpm) >= 0.05f || sync != s_shown_sync)) {
        s_last_text = now;
        bool sync_changed = sync != s_shown_sync;
        s_shown_bpm = bpm; s_shown_sync = sync;

        s_lcd.fillRect(0, 16, 200, 44, TFT_BLACK);
        s_lcd.setTextColor(0xB6FF36u, TFT_BLACK); s_lcd.setTextSize(4);
        s_lcd.setCursor(8, 18);
        if (bpm > 0.0f) s_lcd.printf("%3.0f", bpm); else s_lcd.print("---");
        s_lcd.setTextColor(0x6f8a4du, TFT_BLACK); s_lcd.setTextSize(1);
        s_lcd.setCursor(96, 44); s_lcd.println("BPM");

        s_lcd.fillRect(120, 14, 150, 44, TFT_BLACK);
        s_lcd.setTextColor(sync == 1 ? 0xB6FF36u : 0xFF9D3Bu, TFT_BLACK); s_lcd.setTextSize(1);
        s_lcd.setCursor(130, 16); s_lcd.println(sync == 1 ? "SYNCED" : sync == 0 ? "linking" : "no link");
        s_lcd.setTextColor(TFT_WHITE, TFT_BLACK); s_lcd.setTextSize(1);
        s_lcd.setCursor(130, 34);
        if (g_ap_mode) { s_lcd.setTextColor(0xFF9D3Bu, TFT_BLACK); s_lcd.print("SETUP: join KSTouch-Config -> "); s_lcd.println(g_ks_host); }
        else if (WiFi.status() == WL_CONNECTED) { s_lcd.print(g_ks_host); s_lcd.println(".local"); }
        else s_lcd.println("no wifi");

        if (sync_changed) s_prev_mx = -1;
    }

    // ---- sync wheel marker (top-right) ----
    float phase = tempo_source_phase((float)g_config.quantum_beats);
    if (phase >= 0.0f && g_config.quantum_beats > 0) {
        float ang = fmodf(phase / (float)g_config.quantum_beats, 1.0f) * 360.0f;
        float rad = (ang - 90.0f) * 0.01745329f;
        int mx = WHEEL_CX + (int)(cosf(rad) * (WHEEL_R - 5));
        int my = WHEEL_CY + (int)(sinf(rad) * (WHEEL_R - 5));
        if (mx != s_prev_mx || my != s_prev_my) {
            if (s_prev_mx >= 0) s_lcd.fillCircle(s_prev_mx, s_prev_my, 4, TFT_BLACK);
            s_lcd.drawCircle(WHEEL_CX, WHEEL_CY, WHEEL_R, TFT_DARKGREEN);
            s_lcd.fillCircle(mx, my, 3, TFT_GREEN);
            s_prev_mx = mx; s_prev_my = my;
        }
    }
}

#else  // no touch display on this board -> no-ops
void ktouch_display_begin(void) {}
void ktouch_display_tick(void) {}
#endif
