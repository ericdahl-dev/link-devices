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
static uint32_t          s_rate  = 0;

// ES8311's coefficient table pairs each rate with specific MCLK values: the
// 48k family (8/16/48k) has entries at rate*384, the 44.1k family only at
// rate*256 (11.2896MHz for 44.1k -- rate*384 returns ESP_ERR_INVALID_ARG,
// found on hardware 2026-07-10). Both sides (I2S + codec) must agree.
static uint32_t mclk_multiple_for(uint32_t rate) {
    return (rate % 11025 == 0) ? 256 : AUDIO_BUS_MCLK_MULTIPLE;
}

static esp_err_t i2s_init(uint32_t rate) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // TX underruns emit silence, not the last sample
    esp_err_t e = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);  // full duplex: one clock domain
    if (e != ESP_OK) return e;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate),
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
    std_cfg.clk_cfg.mclk_multiple = (i2s_mclk_multiple_t)mclk_multiple_for(rate);

    e = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (e != ESP_OK) return e;
    e = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (e != ESP_OK) return e;

    // RX starts disabled -- follow_beat_io_start() enables it when its feature
    // is turned on. TX does NOT: hardware validation (2026-07-09) found that
    // with only RX enabled, i2s_channel_read() blocks forever -- only one
    // direction actually drives the shared BCLK/WS clock generator (the header
    // comment's "clock-driver asymmetry" note), and it turned out enabling RX
    // alone never starts it. TX is unconditionally enabled by audio_bus_init()
    // below instead, independent of whether the metronome FEATURE is on, so the
    // clock always runs whenever the bus exists -- metronome_audio_start() just
    // queues/writes click bursts into an already-running channel; it no longer
    // owns enabling TX itself.
    return ESP_OK;
}

static esp_err_t codec_init(uint32_t rate) {
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
        .mclk_frequency = rate * mclk_multiple_for(rate),
        .sample_frequency = rate,
    };
    e = es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (e != ESP_OK) return e;
    e = es8311_sample_frequency_config(s_codec, rate * mclk_multiple_for(rate), rate);
    if (e != ESP_OK) return e;

    // digital_mic=false selects analog mic-in mode (vs. digital/PDM). Confirmed
    // correct against real hardware (2026-07-09): with this setting (plus the
    // gain below), follow_beat locked onto real click tracks across 108-183 BPM
    // within ~1%. The ES8311's mono ADC output turned out to be duplicated into
    // both I2S slots identically (see follow_beat_io.c's capture_task comment),
    // resolving that assumption too.
    e = es8311_microphone_config(s_codec, false);
    if (e != ESP_OK) return e;

    // Mic PGA gain: hardware validation (2026-07-09) found the ADC reporting
    // total silence (confidence exactly 0.0, not just below threshold) with no
    // gain set -- the codec's power-on-reset gain is evidently too low/muted to
    // register a normal room mic signal. 24dB is a reasonable mid-high starting
    // point (Espressif's ES8311 examples commonly use this range for MEMS mic
    // recording); revisit if follow_beat still can't lock or clips.
    return es8311_microphone_gain_set(s_codec, ES8311_MIC_GAIN_24DB);
}

void audio_bus_init(uint32_t sample_rate) {
    if (s_ready) { ESP_LOGW(TAG, "already initialized"); return; }

    ESP_LOGI(TAG, "audio bus: ES8311 codec + I2S full duplex on I2S_NUM_0");
    ESP_LOGI(TAG, "  I2C SCL=%d SDA=%d (ES8311 @ 0x%02x)", PIN_I2C_SCL, PIN_I2C_SDA, ES8311_ADDRRES_0);
    ESP_LOGI(TAG, "  I2S MCLK=%d BCLK=%d WS=%d DOUT=%d DIN=%d  %d Hz",
             PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_DIN, (int)sample_rate);

    esp_err_t e = i2s_init(sample_rate);
    if (e != ESP_OK) { ESP_LOGE(TAG, "I2S init failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }
    e = codec_init(sample_rate);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ES8311 init failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }

    // TX is the shared clock driver (see i2s_init()'s comment above) -- enable
    // it unconditionally so RX always has a clock, regardless of whether the
    // metronome feature itself wants to click. auto_clear (set in i2s_init())
    // means an always-running, nothing-queued TX just emits silence.
    e = i2s_channel_enable(s_tx);
    if (e != ESP_OK) { ESP_LOGE(TAG, "TX channel enable failed: %s -- audio bus unavailable", esp_err_to_name(e)); return; }

    s_rate  = sample_rate;
    s_ready = true;
    ESP_LOGI(TAG, "audio bus ready");
}

bool audio_bus_ready(void) { return s_ready; }
uint32_t audio_bus_sample_rate(void) { return s_rate; }
i2s_chan_handle_t audio_bus_tx(void) { return s_tx; }
i2s_chan_handle_t audio_bus_rx(void) { return s_rx; }
es8311_handle_t   audio_bus_codec(void) { return s_codec; }
