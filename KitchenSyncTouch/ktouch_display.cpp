#include "config.h"        // board -> HAS_TOUCH_DISPLAY; must precede the guard
#include "ktouch_display.h"

// Compiles only for the touch-LCD board (keeps LovyanGFX out of screenless builds).
#ifdef HAS_TOUCH_DISPLAY

#include <Arduino.h>
#include <WiFi.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "app_config.h"
#include "tempo_source.h"

extern AppConfig g_config;

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

#define LCD_BL 46

static LGFX     s_lcd;
static float    s_shown_bpm = -1.0f;
static int      s_shown_sync = -1;
static uint32_t s_last_ms = 0;

static void backlight_on(void) { pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH); }

void ktouch_display_begin(void) {
    s_lcd.init();
    s_lcd.setRotation(6);   // LNK-035: USB exits downward when hand-held
    backlight_on();
    s_lcd.fillScreen(TFT_BLACK);
    s_lcd.setTextColor(0xB6FF36u, TFT_BLACK);   // KitchenSync green
    s_lcd.setTextSize(2);
    s_lcd.setCursor(8, 40);  s_lcd.println("KITCHEN");
    s_lcd.setCursor(8, 62);  s_lcd.println("SYNC");
    s_lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    s_lcd.setTextSize(1);
    s_lcd.setCursor(8, 100); s_lcd.println("TOUCH -- booting");
}

// Redraw only when a value changed (cheap; no flicker). ~5 Hz.
void ktouch_display_tick(void) {
    uint32_t now = millis();
    if (now - s_last_ms < 200) return;
    s_last_ms = now;

    float bpm = tempo_source_bpm();
    int   sync = tempo_source_phase_valid() ? 1 : (tempo_source_active() ? 0 : -1);

    if (fabsf(bpm - s_shown_bpm) < 0.05f && sync == s_shown_sync) return;
    s_shown_bpm = bpm; s_shown_sync = sync;

    s_lcd.fillScreen(TFT_BLACK);
    // header
    s_lcd.setTextColor(0x838d95u, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(8, 10); s_lcd.println("KITCHENSYNC TOUCH");
    // big BPM
    s_lcd.setTextColor(0xB6FF36u, TFT_BLACK); s_lcd.setTextSize(6);
    s_lcd.setCursor(8, 120);
    if (bpm > 0.0f) s_lcd.printf("%3.0f", bpm); else s_lcd.print("---");
    s_lcd.setTextSize(2); s_lcd.setTextColor(0x6f8a4du, TFT_BLACK);
    s_lcd.setCursor(120, 180); s_lcd.println("BPM");
    // sync + ip
    s_lcd.setTextSize(1);
    const char* st = sync == 1 ? "SYNCED" : sync == 0 ? "linking..." : "no link";
    uint32_t sc = sync == 1 ? 0xB6FF36u : 0xFF9D3Bu;
    s_lcd.setTextColor(sc, TFT_BLACK);
    s_lcd.setCursor(8, 250); s_lcd.println(st);
    s_lcd.setTextColor(0x838d95u, TFT_BLACK);
    s_lcd.setCursor(8, 280);
    s_lcd.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "no wifi");
}

#else  // no touch display on this board -> no-ops
void ktouch_display_begin(void) {}
void ktouch_display_tick(void) {}
#endif
