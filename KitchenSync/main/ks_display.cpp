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
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "fw_version.h"
#include "axs5106l.h"     // ESP-026 inc3: pure, host-tested AXS5106L report parser (X32Link)

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

// Touch (AXS5106L, I2C 0x63) — same pins as KitchenSyncTouch/ktouch_display.cpp.
#define TP_SDA   42
#define TP_SCL   41
#define TP_RST   47

static LGFX s_lcd;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_touch   = NULL;

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

// ---- AXS5106L touch over i2c_master (replaces Arduino Wire + pinMode/delay). ------
static void touch_init(void) {
    // Reset pulse on TP_RST, then bring up the I2C bus (order matches the Arduino glue).
    gpio_config_t rst = {};
    rst.pin_bit_mask = 1ULL << TP_RST;
    rst.mode = GPIO_MODE_OUTPUT;
    gpio_config(&rst);
    gpio_set_level((gpio_num_t)TP_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)TP_RST, 1); vTaskDelay(pdMS_TO_TICKS(80));

    i2c_master_bus_config_t bus = {};
    bus.i2c_port                 = I2C_NUM_0;
    bus.sda_io_num               = (gpio_num_t)TP_SDA;
    bus.scl_io_num               = (gpio_num_t)TP_SCL;
    bus.clk_source               = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt        = 7;
    bus.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus, &s_i2c_bus) != ESP_OK) { ESP_LOGE(TAG, "i2c bus init failed"); return; }

    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address  = AXS5106L_I2C_ADDR;
    dev.scl_speed_hz    = 400000;
    if (i2c_master_bus_add_device(s_i2c_bus, &dev, &s_touch) != ESP_OK)
        ESP_LOGE(TAG, "i2c add device failed");
}

// I2C read (write reg 0x01, then read the 14-byte report) + pure parse. The decode
// lives in the host-tested axs5106l.c; this glue owns only the bus transaction.
static bool read_touch(axs_touch_t *t) {
    if (!s_touch) return false;
    uint8_t reg = AXS5106L_TOUCH_REG;
    if (i2c_master_transmit(s_touch, &reg, 1, 50) != ESP_OK) return false;
    uint8_t buf[AXS5106L_REPORT_LEN];
    if (i2c_master_receive(s_touch, buf, sizeof(buf), 50) != ESP_OK) return false;
    return axs5106l_parse(buf, sizeof(buf), t) == 0;
}

static void display_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "display_task: init panel");
    s_lcd.init();
    s_lcd.setRotation(7);   // landscape 320x172, same as the Arduino build
    ks_display_set_brightness(80);
    draw_hello();
    ESP_LOGI(TAG, "display_task: panel up, %dx%d", (int)s_lcd.width(), (int)s_lcd.height());

    touch_init();
    ESP_LOGI(TAG, "display_task: touch ready — tap the glass");

    // Increment 3: echo the last tap on screen + serial to prove the AXS5106L i2c path
    // and the shared axs5106l.c parser. Raw controller coords are portrait-native; the
    // landscape setRotation(7) maps them screen_x = 319 - raw_y, screen_y = 171 - raw_x
    // (verified against a corner tap). A dot is drawn at the mapped point so it lands
    // under the finger. Real hit-testing (the pure ktouch_ui.c) comes with the UI port.
    axs_touch_t t;
    int prev_dx = -1, prev_dy = -1;
    for (;;) {
        if (read_touch(&t) && t.points_len > 0) {
            int rx = t.points[0].x, ry = t.points[0].y;
            int sx = 319 - ry;  if (sx < 0) sx = 0; if (sx > 319) sx = 319;
            int sy = 171 - rx;  if (sy < 0) sy = 0; if (sy > 171) sy = 171;
            ESP_LOGI(TAG, "touch: count=%d raw=(%d,%d) screen=(%d,%d)", t.count, rx, ry, sx, sy);
            if (prev_dx >= 0) s_lcd.fillCircle(prev_dx, prev_dy, 6, TFT_BLACK);
            s_lcd.fillCircle(sx, sy, 6, 0x36B6FFu);
            prev_dx = sx; prev_dy = sy;
            s_lcd.fillRect(0, 120, 160, 26, TFT_BLACK);
            s_lcd.setTextColor(0x36B6FFu, TFT_BLACK); s_lcd.setTextSize(2);
            s_lcd.setCursor(8, 124);
            s_lcd.printf("%d,%d", sx, sy);
        }
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

extern "C" void ks_display_start(void) {
    backlight_init();
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
}

#else  // !CONFIG_KS_TOUCH_DISPLAY -> no display on this build

extern "C" void ks_display_start(void) {}
extern "C" void ks_display_set_brightness(int pct) { (void)pct; }

#endif
