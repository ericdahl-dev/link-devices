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
#include "metronome.h"
#include "metronome_audio.h"
#include "usb_midi_pack.h"
#include "transport.h"        /* Link play/stop -> MIDI Start/Stop (P4-008) */
#include "link_protocol.h"
#include "link_measurement.h"   /* GhostXForm + host->ghost map (P4-009) */
#include "link_phase.h"         /* link_phase_beats_now (session phase) */
#include "link_measure_io.h"    /* unicast ping/pong measurement client */
#include "clock_output.h"       /* per-output division + phase-nudge (P4-010) */

static const char *TAG = "p4hub";

#define MAX_BURST       96   /* re-prime instead of flooding past this backlog */
#define METRO_QUANTUM   4.0  /* beats/bar for the bar-1 accent — Link timeline
                              * carries no meter, so assume 4/4 (P4-006) */
#define METRO_BURST     8    /* re-prime the click grid past this beat backlog */

static P4HubConfig g_cfg;   /* loaded from NVS in app_main; edited via the web UI */

static void clock_out_task(void *arg)
{
    BeatClock   bc;   beat_clock_reset(&bc);
    ClockTicker cts[P4HUB_CLOCK_OUTPUTS];   /* one grid per output (P4-010) */
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) clock_ticker_reset(&cts[i]);
    Metronome   mt;   metronome_reset(&mt);
    Transport   tr;   transport_reset(&tr);
    bool        running = false;
    bool        phase_locked = false;  /* did the last emitting tick use session phase? */
    uint32_t    pulses = 0;
    uint32_t    clicks = 0;
    int64_t     last_log = 0;

    while (1) {
        /* Drive the measurement client (ping/pong -> GhostXForm). Opens its own
         * unicast socket lazily; non-blocking, so it is safe in this 1 ms loop. */
        link_measure_io_poll();

        LinkTimeline tl;
        bool have_session = wifi_link_timeline(&tl) && tl.micros_per_beat > 0;

        if (have_session) {
            /* One shared beat position drives clock-out AND the metronome. Phase-
             * locked once a GhostXForm is committed (P4-009): map our local clock
             * into the session ghost-time domain and read the true session beat, so
             * the emitted tick 0 / downbeat align to the session. Until then, fall
             * back to the free-running local-tempo accumulator (P4-005): correct
             * rate, arbitrary phase. The metronome is self-contained — it clicks
             * even with no USB-MIDI device attached. */
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
             * re-prime the tick + click grids so the boundary realigns instead of
             * dumping a catch-up burst. On dropping back to free-run, restart
             * beat_clock too. */
            if (locked != phase_locked) {
                for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) clock_ticker_reset(&cts[i]);
                metronome_reset(&mt);
                if (!locked) beat_clock_reset(&bc);
                phase_locked = locked;
            }

            if (usb_midi_host_ready() && g_cfg.clock_out_enable) {
                /* Fan the shared beat out to each enabled output at its own
                 * division (ppqn) + phase nudge, onto its own cable (P4-010). */
                for (int o = 0; o < P4HUB_CLOCK_OUTPUTS; o++) {
                    const ClockOutputCfg* oc = &g_cfg.clock[o];
                    if (!oc->enable) continue;
                    int due = clock_output_due(&cts[o], beats, oc->ppqn, oc->phase_mbeats, MAX_BURST);
                    for (int i = 0; i < due; i++) {
                        uint8_t pkt[4];
                        usb_midi_pack_single(oc->cable, 0xF8, pkt);   /* timing clock */
                        usb_midi_host_send(pkt, 4);
                        pulses++;
                    }
                }

                /* Transport: MIDI Start/Stop on the Link play-state edge (P4-008),
                 * fanned to every enabled output's cable. Primes on the first real
                 * StartStopState, so joining mid-play does not fire a spurious Start. */
                TransportAction ta = transport_update(&tr, link_proto_start_stop_seen(),
                                                      link_proto_playing());
                if (ta != TRANSPORT_NONE) {
                    uint8_t status = (ta == TRANSPORT_START) ? 0xFA : 0xFC;
                    for (int o = 0; o < P4HUB_CLOCK_OUTPUTS; o++) {
                        if (!g_cfg.clock[o].enable) continue;
                        uint8_t pkt[4];
                        usb_midi_pack_single(g_cfg.clock[o].cable, status, pkt);
                        usb_midi_host_send(pkt, 4);
                    }
                    ESP_LOGI(TAG, "transport %s", ta == TRANSPORT_START ? "START" : "STOP");
                }
            }

            if (g_cfg.metronome_enable) {
                MetroClick mc = metronome_update(&mt, beats, METRO_QUANTUM, METRO_BURST);
                if (mc != METRO_NONE) {
                    bool accent = (mc == METRO_ACCENT) && g_cfg.metronome_accent;
                    metronome_audio_click(accent);
                    clicks++;
                    ESP_LOGI(TAG, "metronome %-6s beat %.2f  clicks %lu",
                             accent ? "ACCENT" : "click", beats, (unsigned long)clicks);
                }
            }
            running = true;
        } else if (running) {
            /* Session went away — reset so we re-prime cleanly. */
            beat_clock_reset(&bc);
            for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) clock_ticker_reset(&cts[i]);
            metronome_reset(&mt);
            transport_reset(&tr);
            running = false;
            phase_locked = false;
        }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 1000000) {
            last_log = now;
            ESP_LOGI(TAG, "link peers %d  bpm %.2f  phase %s  usb %s  clock TX %lu  play %d",
                     wifi_link_peers(), link_proto_bpm(),
                     link_measurement_current_xform().valid ? "locked" : "free",
                     usb_midi_host_ready() ? "ready" : "-", pulses,
                     link_proto_playing());
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
    if (g_cfg.metronome_enable) metronome_audio_start();   /* codec/I2S only when used */
    xTaskCreate(clock_out_task, "clock_out", 4096, NULL, 6, NULL);
}
