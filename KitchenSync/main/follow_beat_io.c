/*
 * KitchenSync mic capture glue (P4-020) -- reads the shared I2S RX channel
 * (i2s_audio_bus.c, ES8311 mic-in DIN=GPIO11) and feeds the pure follow_beat.c
 * tempo detector. See i2s_audio_bus.h for why this doesn't own its own I2S
 * channel/codec handle.
 */
#include "follow_beat_io.h"
#include "i2s_audio_bus.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "follow_beat_io";

#define READ_BLOCK_FRAMES 256  // ~16ms per block at 16kHz mono

static TaskHandle_t  s_task = NULL;
static volatile bool s_ready = false;
static FollowBeat     s_detector;
static FollowBeatOut  s_latest;
static portMUX_TYPE   s_lock = portMUX_INITIALIZER_UNLOCKED;

static void capture_task(void *arg) {
    (void)arg;
    i2s_chan_handle_t rx = audio_bus_rx();
    // AUDIO_BUS is configured I2S_SLOT_MODE_STEREO (metronome's TX needs
    // stereo out); read stereo frames and use the left channel only -- the
    // mic is wired to one physical channel regardless of slot count.
    int16_t block[READ_BLOCK_FRAMES * 2];
    size_t  bytes_read = 0;
    for (;;) {
        esp_err_t e = i2s_channel_read(rx, block, sizeof(block), &bytes_read, portMAX_DELAY);
        if (e != ESP_OK) { ESP_LOGW(TAG, "i2s read failed: %s", esp_err_to_name(e)); continue; }
        int frames = (int)(bytes_read / sizeof(int16_t) / 2);
        FollowBeatOut out = s_latest;
        for (int i = 0; i < frames; i++) out = follow_beat_push_sample(&s_detector, block[2 * i]);
        portENTER_CRITICAL(&s_lock);
        s_latest = out;
        portEXIT_CRITICAL(&s_lock);
    }
}

void follow_beat_io_start(void) {
    if (!audio_bus_ready()) {
        ESP_LOGE(TAG, "audio bus not ready -- follow beat disabled (call audio_bus_init() first)");
        return;
    }
    follow_beat_reset(&s_detector);
    memset(&s_latest, 0, sizeof(s_latest));

    esp_err_t e = i2s_channel_enable(audio_bus_rx());
    if (e != ESP_OK) { ESP_LOGE(TAG, "RX channel enable failed: %s -- follow beat disabled", esp_err_to_name(e)); return; }

    if (xTaskCreate(capture_task, "follow_beat", 4096, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "capture task create failed -- follow beat disabled");
        return;
    }
    s_ready = true;
    ESP_LOGI(TAG, "follow beat ready");
}

bool follow_beat_io_ready(void) { return s_ready; }

FollowBeatOut follow_beat_io_status(void) {
    portENTER_CRITICAL(&s_lock);
    FollowBeatOut o = s_latest;
    portEXIT_CRITICAL(&s_lock);
    return o;
}
