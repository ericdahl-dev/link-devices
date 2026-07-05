/*
 * P4Hub metronome audio glue (P4-006) — ES8311 codec + I2S tone bursts out the
 * ESP32-P4-NANO onboard speaker. The click/accent DECISION is pure and
 * host-tested (X32Link/metronome.c); this file only makes sound.
 *
 * Hardware (Waveshare ESP32-P4-NANO): ES8311 codec (I2C addr 0x18) feeding an
 * NS4150B power amp. Pin map lifted from the Waveshare esp32-p4-platform
 * "12_I2SCodec" example (examples/esp-idf/12_I2SCodec/main/example_config.h):
 *
 *   I2C:  SCL=GPIO8   SDA=GPIO7   (I2C0, ES8311 @ 0x18)
 *   PA:   GPIO53      (NS4150B enable, active-high)
 *   I2S:  MCLK=GPIO13 BCLK=GPIO12 WS=GPIO10 DOUT=GPIO9 (DIN=GPIO11, mic, unused)
 *
 * None of these collide with the C6/ESP-Hosted SDIO pins (CLK18/CMD19/D14-17,
 * reset 54) or the USB-host Type-A port. These assumptions are LOGGED at boot so
 * the user can confirm against the board silkscreen if a click is silent.
 *
 * A short pre-rendered tone burst (sine + attack/release envelope, no pop) is
 * written from a dedicated player task on a queued click event, so the 1 ms
 * clock_out_task never blocks on the ~50 ms I2S write.
 */
#include "metronome_audio.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "es8311.h"

static const char *TAG = "metro_audio";

/* --- ESP32-P4-NANO pin map (see file header) --------------------------------- */
#define I2C_PORT        I2C_NUM_0
#define PIN_I2C_SCL     GPIO_NUM_8
#define PIN_I2C_SDA     GPIO_NUM_7
#define PIN_PA_ENABLE   GPIO_NUM_53
#define PIN_I2S_MCLK    GPIO_NUM_13
#define PIN_I2S_BCLK    GPIO_NUM_12
#define PIN_I2S_WS      GPIO_NUM_10
#define PIN_I2S_DOUT    GPIO_NUM_9
#define PIN_I2S_DIN     GPIO_NUM_11   /* mic in, unused here */

/* --- Audio format ------------------------------------------------------------ */
#define SAMPLE_RATE     16000
#define MCLK_MULTIPLE   384
#define VOLUME          80            /* 0..100 */

/* --- Tone bursts (compile-time sized; stereo int16) -------------------------- */
#define CLICK_MS        45
#define ACCENT_MS       55
#define CLICK_HZ        1000.0f
#define ACCENT_HZ       1760.0f       /* A6 — clearly above the plain click */
#define CLICK_AMP       0.28f
#define ACCENT_AMP      0.55f
#define CLICK_FRAMES    (SAMPLE_RATE * CLICK_MS  / 1000)
#define ACCENT_FRAMES   (SAMPLE_RATE * ACCENT_MS / 1000)

static int16_t s_click[CLICK_FRAMES * 2];
static int16_t s_accent[ACCENT_FRAMES * 2];

static i2s_chan_handle_t s_tx = NULL;
static es8311_handle_t   s_codec = NULL;
static QueueHandle_t     s_queue = NULL;
static volatile bool     s_ready = false;

/* Render a stereo tone burst with a short linear attack/release so the speaker
 * never gets a step edge (which would pop). */
static void render_burst(int16_t *buf, int frames, float freq, float amp)
{
    const int attack  = SAMPLE_RATE * 4 / 1000;   /* 4 ms */
    const int release = SAMPLE_RATE * 8 / 1000;   /* 8 ms */
    for (int i = 0; i < frames; i++) {
        float env = 1.0f;
        if (i < attack)                 env = (float)i / (float)attack;
        else if (i >= frames - release) env = (float)(frames - i) / (float)release;
        float s = sinf(2.0f * (float)M_PI * freq * (float)i / (float)SAMPLE_RATE) * amp * env;
        int16_t v = (int16_t)(s * 32767.0f);
        buf[2 * i]     = v;   /* L */
        buf[2 * i + 1] = v;   /* R */
    }
}

static void player_task(void *arg)
{
    bool accent;
    while (1) {
        if (xQueueReceive(s_queue, &accent, portMAX_DELAY) != pdTRUE) continue;
        int16_t *buf = accent ? s_accent : s_click;
        size_t   nbytes = (size_t)(accent ? ACCENT_FRAMES : CLICK_FRAMES) * 2 * sizeof(int16_t);
        size_t   written = 0;
        esp_err_t e = i2s_channel_write(s_tx, buf, nbytes, &written, pdMS_TO_TICKS(200));
        if (e != ESP_OK) ESP_LOGW(TAG, "i2s write failed: %s", esp_err_to_name(e));
    }
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   /* underruns emit silence, not the last sample */
    esp_err_t e = i2s_new_channel(&chan_cfg, &s_tx, NULL);   /* TX only, no mic */
    if (e != ESP_OK) return e;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;
    e = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (e != ESP_OK) return e;
    return i2s_channel_enable(s_tx);
}

static esp_err_t codec_init(void)
{
    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t e = i2c_param_config(I2C_PORT, &i2c_cfg);
    if (e != ESP_OK) return e;
    e = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (e != ESP_OK) return e;

    s_codec = es8311_create(I2C_PORT, ES8311_ADDRRES_0);
    if (!s_codec) return ESP_FAIL;

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = SAMPLE_RATE * MCLK_MULTIPLE,
        .sample_frequency = SAMPLE_RATE,
    };
    e = es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (e != ESP_OK) return e;
    e = es8311_sample_frequency_config(s_codec, SAMPLE_RATE * MCLK_MULTIPLE, SAMPLE_RATE);
    if (e != ESP_OK) return e;
    e = es8311_voice_volume_set(s_codec, VOLUME, NULL);
    if (e != ESP_OK) return e;
    return es8311_microphone_config(s_codec, false);
}

void metronome_audio_start(void)
{
    ESP_LOGI(TAG, "metronome audio: ES8311 codec + I2S out the onboard speaker");
    ESP_LOGI(TAG, "  I2C SCL=%d SDA=%d (ES8311 @ 0x%02x)  PA_EN=%d",
             PIN_I2C_SCL, PIN_I2C_SDA, ES8311_ADDRRES_0, PIN_PA_ENABLE);
    ESP_LOGI(TAG, "  I2S MCLK=%d BCLK=%d WS=%d DOUT=%d  %d Hz",
             PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, SAMPLE_RATE);
    ESP_LOGW(TAG, "  ^ pin map from the Waveshare P4-NANO 12_I2SCodec example — "
                  "confirm against the board if the click is silent");

    render_burst(s_click,  CLICK_FRAMES,  CLICK_HZ,  CLICK_AMP);
    render_burst(s_accent, ACCENT_FRAMES, ACCENT_HZ, ACCENT_AMP);

    /* NS4150B power amp enable (active-high). */
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << PIN_PA_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa);
    gpio_set_level(PIN_PA_ENABLE, 1);

    esp_err_t e = i2s_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "I2S init failed: %s — metronome muted", esp_err_to_name(e)); return; }
    e = codec_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "ES8311 init failed: %s — metronome muted", esp_err_to_name(e)); return; }

    s_queue = xQueueCreate(4, sizeof(bool));
    if (!s_queue) { ESP_LOGE(TAG, "queue alloc failed — metronome muted"); return; }
    if (xTaskCreate(player_task, "metro_click", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "player task create failed — metronome muted");
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "metronome audio ready");
}

bool metronome_audio_ready(void) { return s_ready; }

void metronome_audio_click(bool accent)
{
    if (!s_ready) return;
    (void)xQueueSend(s_queue, &accent, 0);   /* non-blocking; drop if backed up */
}
