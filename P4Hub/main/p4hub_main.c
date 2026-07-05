/*
 * P4Hub — ESP32-P4 "hub tier" firmware (P4-002 scaffold).
 *
 * Native ESP-IDF app. The P4 hub features — USB-MIDI host (P4-003/005) and Link
 * over WiFi via the C6 (P4-004) — need ESP-IDF components the Arduino core does
 * not expose; the shipped S3 X32Link stays Arduino. Per ADR-0003 the interesting
 * logic is pure, host-tested C reused UNCHANGED from X32Link, and this app is
 * thin glue.
 *
 * This scaffold proves the reuse end of that split: it drives the pure
 * clock_ticker engine with a simulated 120 BPM beat position and logs the
 * 24-PPQN pulses it schedules. The same engine will later be fed a real Link
 * tempo (P4-004) and emit to the USB-MIDI host (P4-005) instead of a log line.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "clock_ticker.h"

static const char *TAG = "p4hub";

#define SIM_BPM        120.0   /* placeholder tempo until P4-004 feeds Link */
#define MIDI_PPQN      24
#define MAX_BURST      96      /* re-prime instead of flooding past this backlog */

/* Seconds since boot as a double. */
static double uptime_s(void) { return esp_timer_get_time() / 1e6; }

void app_main(void)
{
    ESP_LOGI(TAG, "P4Hub scaffold up — reusing pure clock_ticker (ADR-0003)");

    ClockTicker ticker;
    clock_ticker_reset(&ticker);

    const double  t0 = uptime_s();
    uint32_t      pulses = 0;
    double        last_log = t0;

    while (1) {
        /* Monotonic beat position — stands in for the Link phase pipeline. */
        double beats_now = (uptime_s() - t0) * (SIM_BPM / 60.0);
        pulses += clock_ticker_ticks_due(&ticker, beats_now, MIDI_PPQN, MAX_BURST);

        double now = uptime_s();
        if (now - last_log >= 1.0) {
            last_log = now;
            ESP_LOGI(TAG, "beats %.2f  pulses %lu  (expect ~%d/s at %g BPM x %d PPQN)",
                     beats_now, pulses, (int)(SIM_BPM / 60.0 * MIDI_PPQN), SIM_BPM, MIDI_PPQN);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
