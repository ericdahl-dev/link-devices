// ESP-026 spike, increment 1: bring the Waveshare ESP32-S3-Touch-LCD-1.47 panel up
// under ESP-IDF. Goal of this increment is narrow and deliberate: prove that the
// LovyanGFX Panel_ST7789 + Bus_SPI config that works under Arduino also works under
// ESP-IDF's SPI driver on the S3. No touch, no live tempo yet — those are increments
// 2 and 3. If pixels appear, the one real unknown of the whole migration is dead.
//
// See ks_display.h and ADR-0009. Ported from KitchenSyncTouch/ktouch_display.cpp.
#include "sdkconfig.h"
#include "ks_display.h"

#if CONFIG_KS_TOUCH_DISPLAY

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "fw_version.h"

static const char *TAG = "ks_display";

// ---- Panel: JD9853 via Panel_ST7789, 172x320 visible in 240x320 RAM. -------------
// IDENTICAL to KitchenSyncTouch/ktouch_display.cpp (validated on glass, LNK-014).
// LovyanGFX's LGFX_Device/Panel/Bus classes are framework-agnostic and drive ESP-IDF's
// SPI2_HOST directly — this class body is copied verbatim from the Arduino build.
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
#define SCR_W    320
#define SCR_H    172

static LGFX s_lcd;

// ---- Backlight: ledc PWM on GPIO46 (replaces Arduino analogWrite). ----------------
#define BL_TIMER    LEDC_TIMER_0
#define BL_CHANNEL  LEDC_CHANNEL_0
#define BL_MODE     LEDC_LOW_SPEED_MODE

static void backlight_init(void) {
    ledc_timer_config_t t = {};
    t.speed_mode      = BL_MODE;
    t.duty_resolution = LEDC_TIMER_8_BIT;
    t.timer_num       = BL_TIMER;
    t.freq_hz         = 5000;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    ledc_channel_config_t c = {};
    c.gpio_num   = LCD_BL;
    c.speed_mode = BL_MODE;
    c.channel    = BL_CHANNEL;
    c.timer_sel  = BL_TIMER;
    c.duty       = 0;
    c.hpoint     = 0;
    ledc_channel_config(&c);
}

extern "C" void ks_display_set_brightness(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ledc_set_duty(BL_MODE, BL_CHANNEL, (uint32_t)(pct * 255 / 100));
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

// ---- Increment 1 test frame: header + a big "OK" so a working panel is unmistakable.
static void draw_hello(void) {
    s_lcd.fillScreen(TFT_BLACK);
    s_lcd.setTextColor(0x838d95u, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(8, 4); s_lcd.println("KITCHENSYNC TOUCH");

    s_lcd.setTextColor(0xB6FF36u, TFT_BLACK); s_lcd.setTextSize(4);
    s_lcd.setCursor(8, 40); s_lcd.println("ESP-IDF");
    s_lcd.setCursor(8, 84); s_lcd.println("DISPLAY OK");

    s_lcd.setTextColor(0x6f8a4du, TFT_BLACK); s_lcd.setTextSize(1);
    s_lcd.setCursor(8, SCR_H - 14); s_lcd.print("fw "); s_lcd.println(FW_VERSION);
}

static void display_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "display_task: init panel");
    s_lcd.init();
    s_lcd.setRotation(7);   // landscape 320x172, same as the Arduino build
    ks_display_set_brightness(80);
    draw_hello();
    ESP_LOGI(TAG, "display_task: panel up, %dx%d", (int)s_lcd.width(), (int)s_lcd.height());

    // Increment 2 will replace this idle loop with live BPM/sync from beat_source.
    for (;;) vTaskDelay(pdMS_TO_TICKS(100));
}

extern "C" void ks_display_start(void) {
    backlight_init();
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
}

#else  // !CONFIG_KS_TOUCH_DISPLAY -> no display on this build

extern "C" void ks_display_start(void) {}
extern "C" void ks_display_set_brightness(int pct) { (void)pct; }

#endif
