/*
 * P4Hub — ESP32-P4 hub-tier firmware. P4-005: a live Ableton Link tempo drives
 * 24-PPQN MIDI clock out the USB-MIDI host to attached gear (e.g. a Blokas
 * Midihub). The spine of the full-featured MIDI-clock / Link device.
 *
 * Data flow, per ADR-0003 (pure logic host-tested; glue thin):
 *   wifi_link (Link gossip)  -> LinkTimeline.micros_per_beat
 *     -> beat_clock   (pure) : integrate tempo over the local clock -> beats
 *     -> clock_ticker (pure) : quantize beats to 24 PPQN pulses
 *     -> usb_midi_pack(pure) : encode 0xF8 as a USB-MIDI event packet
 *     -> usb_midi_host (glue): bulk-OUT to the device
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "wifi_link.h"
#include "usb_midi_host.h"
#include "beat_clock.h"
#include "clock_ticker.h"
#include "usb_midi_pack.h"
#include "link_protocol.h"

static const char *TAG = "p4hub";

#define MIDI_PPQN   24
#define MAX_BURST   96   /* re-prime instead of flooding past this backlog */
#define MIDI_CABLE  0    /* USB-MIDI virtual cable 0 (Midihub "USB A"); per-output
                            cable/division/phase is a Multiclock-style follow-up */

static void clock_out_task(void *arg)
{
    BeatClock   bc;   beat_clock_reset(&bc);
    ClockTicker ct;   clock_ticker_reset(&ct);
    bool        running = false;
    uint32_t    pulses = 0;
    int64_t     last_log = 0;

    while (1) {
        LinkTimeline tl;
        bool have_session = wifi_link_timeline(&tl) && tl.micros_per_beat > 0;

        if (have_session && usb_midi_host_ready()) {
            double beats = beat_clock_advance(&bc, esp_timer_get_time(), tl.micros_per_beat);
            int due = clock_ticker_ticks_due(&ct, beats, MIDI_PPQN, MAX_BURST);
            for (int i = 0; i < due; i++) {
                uint8_t pkt[4];
                usb_midi_pack_single(MIDI_CABLE, 0xF8, pkt);   /* timing clock */
                usb_midi_host_send(pkt, 4);
                pulses++;
            }
            running = true;
        } else if (running) {
            /* Session or device went away — reset so we re-prime cleanly. */
            beat_clock_reset(&bc);
            clock_ticker_reset(&ct);
            running = false;
        }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 1000000) {
            last_log = now;
            ESP_LOGI(TAG, "link peers %d  bpm %.2f  usb %s  clock TX %lu  RX(loopback) %lu",
                     wifi_link_peers(), link_proto_bpm(),
                     usb_midi_host_ready() ? "ready" : "-", pulses,
                     usb_midi_host_rx_clocks());
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "P4Hub — Link tempo -> USB-MIDI host clock out (P4-005)");
    usb_midi_host_start();
    wifi_link_start();
    xTaskCreate(clock_out_task, "clock_out", 4096, NULL, 6, NULL);
}
