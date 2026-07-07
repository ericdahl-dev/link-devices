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
#include "beat_source.h"      /* basis selector: session phase vs free-run (ARC-007) */
#include "clock_ticker.h"
#include "metronome.h"
#include "metronome_audio.h"
#include "metro_strip.h"     /* pure WS2812 bar-position chase (P4-018) */
#include "p4hub_led.h"       /* WS2812 strip glue (RMT) */
#include "usb_midi_pack.h"
#include "transport.h"        /* Link play/stop -> MIDI Start/Stop (P4-008) */
#include "link_protocol.h"
#include "link_measurement.h"   /* GhostXForm accessor (link_measurement_current_xform) */
#include "link_measure_io.h"    /* unicast ping/pong measurement client */
#include "clock_output.h"       /* per-output division + phase-nudge (P4-010) */

static const char *TAG = "p4hub";

#define MAX_BURST       96   /* re-prime instead of flooding past this backlog */
#define METRO_QUANTUM   4.0  /* beats/bar for the bar-1 accent — Link timeline
                              * carries no meter, so assume 4/4 (P4-006) */
#define METRO_BURST     8    /* re-prime the click grid past this beat backlog */
#define LED_GPIO        2    /* WS2812 data pin (P4-018); external strip + 5V/GND */
#define LED_PIXELS      METRO_STRIP_PIXELS
#define LED_FRAME_US    20000 /* ~50 fps strip refresh, decoupled from the 1 ms clock */

static P4HubConfig g_cfg;   /* loaded from NVS in app_main; edited via the web UI */
static volatile uint32_t g_cfg_gen = 0;   /* bumped by web POST /live; clock task re-primes on change (P4-015) */

static void clock_out_task(void *arg)
{
    BeatSource  src;  beat_source_reset(&src);
    ClockTicker cts[P4HUB_CLOCK_OUTPUTS];   /* one grid per output (P4-010) */
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) clock_ticker_reset(&cts[i]);
    Metronome   mt;   metronome_reset(&mt);
    Transport   tr;   transport_reset(&tr);
    uint32_t    pulses = 0;
    uint32_t    clicks = 0;
    int64_t     last_log = 0;
    uint32_t    seen_gen = g_cfg_gen;   /* re-prime when the web UI live-edits timing (P4-015) */
    int64_t     last_led = 0;           /* throttle the WS2812 refresh (P4-018) */
    bool        led_showing = false;    /* is the strip currently lit (to clear once when off) */

    while (1) {
        /* Drive the measurement client (ping/pong -> GhostXForm). Opens its own
         * unicast socket lazily; non-blocking, so it is safe in this 1 ms loop. */
        link_measure_io_poll();

        LinkTimeline tl;
        bool have_session = wifi_link_timeline(&tl) && tl.micros_per_beat > 0;

        /* beat_source (ARC-007) owns the basis policy: phase-locked session beat
         * once a GhostXForm is committed (P4-009, true downbeat) vs the free-
         * running local accumulator (P4-005, correct rate / arbitrary phase), plus
         * the re-prime signal on any basis switch or session loss. One shared beat
         * drives clock-out AND the self-contained metronome. */
        BeatSourceOut bs = beat_source_step(&src, have_session,
                                            link_measurement_current_xform(),
                                            tl, esp_timer_get_time());

        /* A basis switch, session loss, or a live config edit (P4-015: phase /
         * division / swing changed via POST /live) shifts the beat grid; re-prime
         * the tick + click grids so the boundary realigns instead of dumping a
         * catch-up burst. On the session-loss edge, also reset transport so a later
         * re-join does not fire a spurious Start. */
        bool reprime = bs.reprime;
        if (g_cfg_gen != seen_gen) { reprime = true; seen_gen = g_cfg_gen; }
        if (reprime) {
            for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) clock_ticker_reset(&cts[i]);
            metronome_reset(&mt);
            if (!bs.active) transport_reset(&tr);
        }

        if (bs.active) {
            double beats = bs.beats;

            if (usb_midi_host_ready() && g_cfg.clock_out_enable) {
                /* Fan the shared beat out to each enabled output at its own
                 * division (ppqn) + phase nudge, onto its own cable (P4-010). */
                for (int o = 0; o < P4HUB_CLOCK_OUTPUTS; o++) {
                    const ClockOutputCfg* oc = &g_cfg.clock[o];
                    if (!oc->enable) continue;
                    int due = clock_output_due(&cts[o], beats, oc->ppqn, oc->phase_mbeats, oc->swing_mbeats, MAX_BURST);
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
        }

        int64_t now = esp_timer_get_time();

        /* Visual metronome (P4-018): render the pure bar-position chase from the
         * shared beat onto the WS2812 strip, throttled to ~50 fps so it doesn't
         * steal time from the 1 ms clock loop. Independent of the audio metronome
         * (its own led_enable switch); clears once when disabled or idle. */
        if (now - last_led >= LED_FRAME_US) {
            last_led = now;
            if (g_cfg.led_enable && bs.active) {
                RGB frame[LED_PIXELS];
                metro_strip_render(bs.beats, (int)METRO_QUANTUM, LED_PIXELS, frame);
                p4hub_led_show(frame, LED_PIXELS);
                led_showing = true;
            } else if (led_showing) {
                p4hub_led_clear();
                led_showing = false;
            }
        }

        if (now - last_log >= 1000000) {
            last_log = now;
            ESP_LOGI(TAG, "link peers %d  bpm %.2f  phase %s  usb %s  clock TX %lu  play %d",
                     wifi_link_peers(), link_proto_bpm(),
                     bs.locked ? "locked" : "free",
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
    p4hub_web_start(&g_cfg, &g_cfg_gen);
    if (g_cfg.metronome_enable)
        metronome_audio_start(g_cfg.metronome_volume, g_cfg.metronome_voice);   /* codec/I2S only when used */
    p4hub_led_start(LED_GPIO, LED_PIXELS);   /* WS2812 visual metronome; harmless if nothing wired (P4-018) */
    xTaskCreate(clock_out_task, "clock_out", 4096, NULL, 6, NULL);
}
