#include "config.h"        // board -> HAS_TOUCH_DISPLAY; must precede the guard
#include "ktouch_display.h"

// Compiles only for the touch-LCD board (keeps LovyanGFX out of screenless builds).
#ifdef HAS_TOUCH_DISPLAY

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "app_config.h"
#include "tempo_source.h"

extern AppConfig g_config;
extern char g_ks_host[];   // mDNS hostname, set by the .ino (e.g. "kstouch-1a2b")

// Panel: JD9853 via LovyanGFX Panel_ST7789 (validated on glass, LNK-014). Same
// pins/geometry as X32Link/touch_display.cpp. 172x320 visible in 240x320 RAM.
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
// Landscape 320x172 (setRotation 7): a transport device is held horizontally, so
// there's width for a big BPM readout, the sync wheel, a readable name -- and, in
// Inc2, two fat side-by-side PLAY/STOP buttons across the bottom.
#define SCR_W    320
#define SCR_H    172
#define WHEEL_CX 255
#define WHEEL_CY 74
#define WHEEL_R  50

static LGFX     s_lcd;
static float    s_shown_bpm = -1.0f;
static int      s_shown_sync = -2;
static int      s_prev_mx = -1, s_prev_my = -1;
static uint32_t s_last_text = 0;

static void backlight_on(void) { pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH); }

// Static frame: header, wheel outline, "SYNC" label. Redrawn on a sync-state change.
static void draw_frame(int sync) {
    s_lcd.fillScreen(TFT_BLACK);
    s_lcd.setTextColor(0x838d95u, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(10, 8); s_lcd.println("KITCHENSYNC TOUCH");
    s_lcd.drawCircle(WHEEL_CX, WHEEL_CY, WHEEL_R, TFT_DARKGREEN);
    s_lcd.setTextColor(0x6f8a4du, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(WHEEL_CX - 12, WHEEL_CY + WHEEL_R + 6); s_lcd.println("SYNC");
    s_prev_mx = s_prev_my = -1;
    (void)sync;
}

void ktouch_display_begin(void) {
    s_lcd.init();
    s_lcd.setRotation(7);   // landscape 320x172 (transport device held horizontally)
    backlight_on();
    draw_frame(-1);
    s_lcd.setTextColor(0xB6FF36u, TFT_BLACK); s_lcd.setTextSize(2);
    s_lcd.setCursor(10, 140); s_lcd.println("booting...");
}

void ktouch_display_tick(void) {
    float bpm  = tempo_source_bpm();
    int   sync = tempo_source_phase_valid() ? 1 : (tempo_source_active() ? 0 : -1);
    uint32_t now = millis();

    // Text (BPM number + sync line + name): throttled, redraw on change.
    if (now - s_last_text >= 200 &&
        (fabsf(bpm - s_shown_bpm) >= 0.05f || sync != s_shown_sync)) {
        s_last_text = now;
        if (sync != s_shown_sync) draw_frame(sync);  // clears wheel interior too
        s_shown_bpm = bpm; s_shown_sync = sync;

        // Big BPM, top-left.
        s_lcd.fillRect(0, 32, 200, 60, TFT_BLACK);
        s_lcd.setTextColor(0xB6FF36u, TFT_BLACK); s_lcd.setTextSize(7);
        s_lcd.setCursor(10, 34);
        if (bpm > 0.0f) s_lcd.printf("%3.0f", bpm); else s_lcd.print("---");
        s_lcd.setTextColor(0x6f8a4du, TFT_BLACK); s_lcd.setTextSize(2);
        s_lcd.setCursor(150, 94); s_lcd.println("BPM");

        // Sync state + name across the bottom, both large.
        s_lcd.fillRect(0, 118, SCR_W, 54, TFT_BLACK);
        const char* st = sync == 1 ? "SYNCED" : sync == 0 ? "linking..." : "no link";
        s_lcd.setTextColor(sync == 1 ? 0xB6FF36u : 0xFF9D3Bu, TFT_BLACK); s_lcd.setTextSize(2);
        s_lcd.setCursor(10, 120); s_lcd.println(st);
        // Reach it by name, not a faint IP. Bigger + white.
        s_lcd.setTextColor(TFT_WHITE, TFT_BLACK); s_lcd.setTextSize(2);
        s_lcd.setCursor(10, 146);
        if (WiFi.status() == WL_CONNECTED) { s_lcd.print(g_ks_host); s_lcd.println(".local"); }
        else s_lcd.println("no wifi");
    }

    // Wheel marker: sweeps every tick so the beat/downbeat is visible in motion.
    float phase = tempo_source_phase((float)g_config.quantum_beats);
    if (phase >= 0.0f && g_config.quantum_beats > 0) {
        float ang = fmodf(phase / (float)g_config.quantum_beats, 1.0f) * 360.0f;
        float rad = (ang - 90.0f) * 0.01745329f;   // 0deg at top
        int mx = WHEEL_CX + (int)(cosf(rad) * (WHEEL_R - 8));
        int my = WHEEL_CY + (int)(sinf(rad) * (WHEEL_R - 8));
        if (mx != s_prev_mx || my != s_prev_my) {
            if (s_prev_mx >= 0) s_lcd.fillCircle(s_prev_mx, s_prev_my, 7, TFT_BLACK);
            s_lcd.drawCircle(WHEEL_CX, WHEEL_CY, WHEEL_R, TFT_DARKGREEN);  // repair outline
            s_lcd.fillCircle(mx, my, 6, TFT_GREEN);
            s_prev_mx = mx; s_prev_my = my;
        }
    }
}

#else  // no touch display on this board -> no-ops
void ktouch_display_begin(void) {}
void ktouch_display_tick(void) {}
#endif
