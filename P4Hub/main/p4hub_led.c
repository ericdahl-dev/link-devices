#include "p4hub_led.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "p4hub_led";
static led_strip_handle_t s_strip = NULL;

void p4hub_led_start(int gpio, int npix) {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = gpio,
        .max_leds         = npix,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,   // WS2812 order
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,          // 10 MHz -> WS2812 timing
        .flags.with_dma = false,
    };
    esp_err_t e = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s — visual metronome disabled", esp_err_to_name(e));
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 x%d on GPIO%d", npix, gpio);
}

void p4hub_led_show(const RGB* px, int npix) {
    if (!s_strip) return;
    for (int i = 0; i < npix; i++) led_strip_set_pixel(s_strip, i, px[i].r, px[i].g, px[i].b);
    led_strip_refresh(s_strip);
}

void p4hub_led_clear(void) {
    if (s_strip) led_strip_clear(s_strip);
}
