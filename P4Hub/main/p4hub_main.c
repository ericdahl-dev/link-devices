/*
 * P4Hub — ESP32-P4 hub-tier firmware. P4-005: a live Ableton Link tempo drives
 * 24-PPQN MIDI clock out the USB-MIDI host to attached gear (e.g. a Blokas
 * Midihub). The spine of the full-featured MIDI-clock / Link device.
 *
 * Data flow, per ADR-0003 (pure logic host-tested; glue thin):
 *   wifi_link (Link gossip)  -> LinkTimeline (tempo + ghost-time origin)
 *     -> [P4-009 phase-lock] link_measure_io (glue): unicast ping/pong -> GhostXForm
 *          -> link_phase   (pure) : host->ghost time -> true SESSION beats
 *        [P4-005 fallback] beat_clock (pure): integrate tempo over local clock -> beats
 *     -> clock_ticker (pure) : quantize beats to 24 PPQN pulses
 *     -> usb_midi_pack(pure) : encode 0xF8 as a USB-MIDI event packet
 *     -> usb_midi_host (glue): bulk-OUT to the device
 *
 * P4-009: when a committed GhostXForm exists, the emitted 24-PPQN clock aligns to
 * the Link session's actual beat/bar (downbeat), not just its rate — so a future
 * MIDI Start (P4-008) lands on beat 1 and multiple synced devices agree on the
 * downbeat. Until the first measurement commits (or after a transport re-origin
 * invalidates it), we fall back to the free-running local-tempo accumulator.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "wifi_link.h"
#include "usb_midi_host.h"
#include "p4hub_web.h"
#include "p4hub_config.h"
#include "p4hub_config_nvs.h"
#include "beat_clock.h"
#include "clock_ticker.h"
#include "usb_midi_pack.h"
#include "link_protocol.h"
#include "link_measurement.h"   /* GhostXForm + host->ghost map (P4-009) */
#include "link_phase.h"         /* link_phase_beats_now (session phase) */
#include "link_measure_io.h"    /* unicast ping/pong measurement client */

static const char *TAG = "p4hub";

#define MIDI_PPQN   24
#define MAX_BURST   96   /* re-prime instead of flooding past this backlog */

static P4HubConfig g_cfg;   /* loaded from NVS in app_main; edited via the web UI */

static void clock_out_task(void *arg)
{
    BeatClock   bc;   beat_clock_reset(&bc);
    ClockTicker ct;   clock_ticker_reset(&ct);
    bool        running = false;
    bool        phase_locked = false;  /* did the last emitting tick use session phase? */
    uint32_t    pulses = 0;
    int64_t     last_log = 0;

    while (1) {
        /* Drive the measurement client (ping/pong -> GhostXForm). Opens its own
         * unicast socket lazily; non-blocking, so it is safe in this 1 ms loop. */
        link_measure_io_poll();

        LinkTimeline tl;
        bool have_session = wifi_link_timeline(&tl) && tl.micros_per_beat > 0;

        if (have_session && usb_midi_host_ready() && g_cfg.clock_out_enable) {
            /* Phase-locked once a GhostXForm is committed (P4-009): map our local
             * clock into the session's ghost-time domain and read the true session
             * beat, so tick 0 lands on the session downbeat. Otherwise fall back to
             * the free-running local-tempo accumulator (P4-005): correct rate,
             * arbitrary phase. Same math as the S3's tempo_source.cpp. */
            LinkGhostXForm xform = link_measurement_current_xform();
            bool   locked = xform.valid;
            double beats;
            if (locked) {
                int64_t ghost_now = link_ghost_xform_host_to_ghost(xform, esp_timer_get_time());
                beats = link_phase_beats_now(tl, ghost_now);
            } else {
                beats = beat_clock_advance(&bc, esp_timer_get_time(), tl.micros_per_beat);
            }

            /* Switching basis (free-run <-> session phase) shifts the beat origin;
             * re-prime the ticker so the boundary realigns instead of dumping a
             * catch-up burst. On dropping back to free-run, restart beat_clock too. */
            if (locked != phase_locked) {
                clock_ticker_reset(&ct);
                if (!locked) beat_clock_reset(&bc);
                phase_locked = locked;
            }

            int due = clock_ticker_ticks_due(&ct, beats, MIDI_PPQN, MAX_BURST);
            for (int i = 0; i < due; i++) {
                uint8_t pkt[4];
                usb_midi_pack_single(g_cfg.midi_cable, 0xF8, pkt);   /* timing clock */
                usb_midi_host_send(pkt, 4);
                pulses++;
            }
            running = true;
        } else if (running) {
            /* Session or device went away — reset so we re-prime cleanly. */
            beat_clock_reset(&bc);
            clock_ticker_reset(&ct);
            running = false;
            phase_locked = false;
        }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 1000000) {
            last_log = now;
            ESP_LOGI(TAG, "link peers %d  bpm %.2f  phase %s  usb %s  clock TX %lu  RX(loopback) %lu",
                     wifi_link_peers(), link_proto_bpm(),
                     link_measurement_current_xform().valid ? "locked" : "free",
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

    p4hub_config_load(&g_cfg);
    ESP_LOGI(TAG, "P4Hub — Link tempo -> USB-MIDI host clock out (P4-005/007)");
    usb_midi_host_start();
    wifi_link_start(g_cfg.wifi_ssid, g_cfg.wifi_pass);
    p4hub_web_start(&g_cfg);
    xTaskCreate(clock_out_task, "clock_out", 4096, NULL, 6, NULL);
}
