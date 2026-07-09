/*
 * KitchenSync metronome audio glue (P4-006) — ES8311 codec + I2S tone bursts out the
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
#include "metronome_voice.h"    /* click/accent tone presets (P4-012) */
#include "i2s_audio_bus.h"      /* shared I2S_NUM_0 + ES8311 codec owner (P4-020) */

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "es8311.h"

static const char *TAG = "metro_audio";

/* --- ESP32-P4-NANO pin map (see file header) --------------------------------- */
#define PIN_PA_ENABLE   GPIO_NUM_53

/* --- Audio format --------------------------------------------------------------
 * Tied to the shared bus's constant (both are 16000) so this file can never
 * silently drift out of sync with the I2S/codec bring-up in i2s_audio_bus.c. */
#define SAMPLE_RATE     AUDIO_BUS_SAMPLE_RATE

/* --- Tone bursts: pitch + length are runtime (voice preset, P4-012); the amp is
 * fixed per role. Buffers are sized for the longest voice; s_*_frames holds the
 * live length. --------------------------------------------------------------- */
#define CLICK_AMP       0.28f
#define ACCENT_AMP      0.55f
#define MAX_MS          64
#define MAX_FRAMES      (SAMPLE_RATE * MAX_MS / 1000)

static int16_t s_click[MAX_FRAMES * 2];
static int16_t s_accent[MAX_FRAMES * 2];
static int     s_click_frames  = 0;
static int     s_accent_frames = 0;
static int     s_volume = 80;   /* 0..100, set at start from config (P4-012) */

static i2s_chan_handle_t s_tx = NULL;
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
        size_t   nbytes = (size_t)(accent ? s_accent_frames : s_click_frames) * 2 * sizeof(int16_t);
        size_t   written = 0;
        esp_err_t e = i2s_channel_write(s_tx, buf, nbytes, &written, pdMS_TO_TICKS(200));
        if (e != ESP_OK) ESP_LOGW(TAG, "i2s write failed: %s", esp_err_to_name(e));
    }
}

void metronome_audio_start(int volume, int voice)
{
    if (!audio_bus_ready()) {
        ESP_LOGE(TAG, "audio bus not ready -- metronome muted (call audio_bus_init() first)");
        return;
    }
    s_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);

    /* Voice preset -> click/accent pitch + length (P4-012). Lengths are clamped
     * to the buffer; the amp stays fixed per role. */
    float click_hz, accent_hz;
    int   click_ms, accent_ms;
    metronome_voice_params(voice, &click_hz, &click_ms, &accent_hz, &accent_ms);
    s_click_frames  = SAMPLE_RATE * click_ms  / 1000;
    s_accent_frames = SAMPLE_RATE * accent_ms / 1000;
    if (s_click_frames  > MAX_FRAMES) s_click_frames  = MAX_FRAMES;
    if (s_accent_frames > MAX_FRAMES) s_accent_frames = MAX_FRAMES;

    ESP_LOGI(TAG, "metronome audio: ES8311 codec + I2S out the onboard speaker  vol=%d voice=%d", s_volume, voice);

    render_burst(s_click,  s_click_frames,  click_hz,  CLICK_AMP);
    render_burst(s_accent, s_accent_frames, accent_hz, ACCENT_AMP);

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

    s_tx = audio_bus_tx();
    es8311_voice_volume_set(audio_bus_codec(), s_volume, NULL);
    esp_err_t e = i2s_channel_enable(s_tx);
    if (e != ESP_OK) { ESP_LOGE(TAG, "TX channel enable failed: %s -- metronome muted", esp_err_to_name(e)); return; }

    s_queue = xQueueCreate(4, sizeof(bool));
    if (!s_queue) { ESP_LOGE(TAG, "queue alloc failed -- metronome muted"); return; }
    if (xTaskCreate(player_task, "metro_click", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "player task create failed -- metronome muted");
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "metronome audio ready");
}

bool metronome_audio_ready(void) { return s_ready; }

/* P4-029: re-apply volume + voice at runtime (no reboot) so the web /live path can
 * preview them like the LED colours. No-op until _ready() — the codec/I2S are only
 * brought up at boot when the metronome was enabled, so enabling it still needs a
 * reboot; only volume/voice of an already-running metronome are live. Re-renders the
 * two short bursts and re-sets the codec volume; a click landing mid-re-render is at
 * worst one slightly-off burst, which is fine for a user-driven config edit. */
void metronome_audio_set(int volume, int voice)
{
    if (!s_ready) return;
    s_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    es8311_voice_volume_set(audio_bus_codec(), s_volume, NULL);

    float click_hz, accent_hz;
    int   click_ms, accent_ms;
    metronome_voice_params(voice, &click_hz, &click_ms, &accent_hz, &accent_ms);
    int cf = SAMPLE_RATE * click_ms  / 1000;
    int af = SAMPLE_RATE * accent_ms / 1000;
    if (cf > MAX_FRAMES) cf = MAX_FRAMES;
    if (af > MAX_FRAMES) af = MAX_FRAMES;
    render_burst(s_click,  cf, click_hz,  CLICK_AMP);
    render_burst(s_accent, af, accent_hz, ACCENT_AMP);
    s_click_frames  = cf;   /* publish lengths after the buffers are rendered */
    s_accent_frames = af;
    ESP_LOGI(TAG, "metronome audio live: vol=%d voice=%d", s_volume, voice);
}

void metronome_audio_click(bool accent)
{
    if (!s_ready) return;
    (void)xQueueSend(s_queue, &accent, 0);   /* non-blocking; drop if backed up */
}
