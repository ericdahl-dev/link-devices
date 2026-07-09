/*
 * KitchenSync shared I2S bus + ES8311 codec owner (P4-020). See
 * i2s_audio_bus.h for why this exists: metronome_audio.c (TX) and
 * follow_beat_io.c (RX) can't each independently master I2S_NUM_0.
 *
 * Pin map (Waveshare ESP32-P4-NANO, same as metronome_audio.c's original
 * table -- see that file's P4-006 header comment for the full derivation):
 *   I2C:  SCL=GPIO8   SDA=GPIO7   (I2C0, ES8311 @ 0x18)
 *   I2S:  MCLK=GPIO13 BCLK=GPIO12 WS=GPIO10 DOUT=GPIO9 DIN=GPIO11
 */
#include "i2s_audio_bus.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "audio_bus";

#define I2C_PORT      I2C_NUM_0
#define PIN_I2C_SCL   GPIO_NUM_8
#define PIN_I2C_SDA   GPIO_NUM_7
#define PIN_I2S_MCLK  GPIO_NUM_13
#define PIN_I2S_BCLK  GPIO_NUM_12
#define PIN_I2S_WS    GPIO_NUM_10
#define PIN_I2S_DOUT  GPIO_NUM_9
#define PIN_I2S_DIN   GPIO_NUM_11

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;
static es8311_handle_t   s_codec = NULL;
static volatile bool     s_ready = false;

static esp_err_t i2s_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // TX underruns emit silence, not the last sample
    esp_err_t e = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);  // full duplex: one clock domain
    if (e != ESP_OK) return e;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_BUS_SAMPLE_RATE),
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
    std_cfg.clk_cfg.mclk_multiple = AUDIO_BUS_MCLK_MULTIPLE;

    e = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (e != ESP_OK) return e;
    e = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (e != ESP_OK) return e;

    // Both channels start disabled -- metronome_audio_start()/follow_beat_io_start()
    // enable their own direction when their feature is actually turned on.
    return ESP_OK;
}

static esp_err_t codec_init(void) {
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
        .mclk_frequency = AUDIO_BUS_SAMPLE_RATE * AUDIO_BUS_MCLK_MULTIPLE,
        .sample_frequency = AUDIO_BUS_SAMPLE_RATE,
    };
    e = es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (e != ESP_OK) return e;
    e = es8311_sample_frequency_config(s_codec, AUDIO_BUS_SAMPLE_RATE * AUDIO_BUS_MCLK_MULTIPLE,
                                        AUDIO_BUS_SAMPLE_RATE);
    if (e != ESP_OK) return e;

    // digital_mic=false selects analog mic-in mode (vs. digital/PDM). Matches the
    // pre-P4-020 metronome_audio.c precedent, which called this the same way even
    // on its TX-only path -- assumed correct for this board's wiring, final
    // confirmation against real hardware happens at the P4-020 hardware-validation
    // step (see docs/plans/2026-07-09-p4-020-follow-beat-design.md).
    return es8311_microphone_config(s_codec, false);
}

void audio_bus_init(void) {
    if (s_ready) { ESP_LOGW(TAG, "already initialized"); return; }

    ESP_LOGI(TAG, "audio bus: ES8311 codec + I2S full duplex on I2S_NUM_0");
    ESP_LOGI(TAG, "  I2C SCL=%d SDA=%d (ES8311 @ 0x%02x)", PIN_I2C_SCL, PIN_I2C_SDA, ES8311_ADDRRES_0);
    ESP_LOGI(TAG, "  I2S MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d  %d Hz",
             PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, AUDIO_BUS_SAMPLE_RATE);

    esp_err_t e = i2s_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "I2S init failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }
    e = codec_init();
    if (e != ESP_OK) { ESP_LOGE(TAG, "ES8311 init failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }

    s_ready = true;
    ESP_LOGI(TAG, "audio bus ready");
}

bool audio_bus_ready(void) { return s_ready; }
i2s_chan_handle_t audio_bus_tx(void) { return s_tx; }
i2s_chan_handle_t audio_bus_rx(void) { return s_rx; }
es8311_handle_t   audio_bus_codec(void) { return s_codec; }
