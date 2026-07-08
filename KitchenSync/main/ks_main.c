/*
 * KitchenSync — ESP32-P4 hub-tier firmware. P4-005: a live Ableton Link tempo drives
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
#include <math.h>   /* fmod, for the PHASE_DEBUG trace */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "wifi_link.h"
#include "usb_midi_host.h"
#include "ks_web.h"
#include "ks_config.h"
#include "ks_config_nvs.h"
#include "beat_source.h"      /* basis selector: session phase vs free-run (ARC-007) */
#include "clock_ticker.h"
#include "metronome.h"
#include "metronome_audio.h"
#include "metro_strip.h"     /* pure WS2812 bar-position chase (P4-018) */
#include "ks_led.h"       /* WS2812 strip glue (RMT) */
#include "usb_midi_pack.h"
#include "transport.h"        /* Link play/stop -> MIDI Start/Stop (P4-008) */
#include "link_protocol.h"
#include "link_measurement.h"   /* GhostXForm accessor (link_measurement_current_xform) */
#include "link_measure_io.h"    /* unicast ping/pong measurement client */
#include "clock_output.h"       /* per-output division + phase-nudge (P4-010) */
#include "ks_tick.h"            /* ARC-015: pure clock-tick orchestration */

static const char *TAG = "kitchensync";

/* Tick-loop policy constants moved to ks_tick.h (KS_TICK_*, ARC-015). */
#define LED_GPIO        2    /* WS2812 data pin (P4-018); external strip + 5V/GND */
#define LED_PIXELS      METRO_STRIP_PIXELS
#define LED_FRAME_US    20000 /* ~50 fps strip refresh, decoupled from the 1 ms clock */

/* Phase debug (LNK-026 offset hunt): log the P4's computed session phase + the raw
 * ghost-xform/timeline ingredients so it can be diffed against a ground-truth Link
 * reference (Ableton Live metronome / Link SDK on the Mac). At Live's downbeat the
 * P4's `phase` should read ~0; if it reads ~2, that's the static offset, and the
 * intercept / beat_origin / ghost_now fields show where it enters. Set 0 to silence. */
#define PHASE_DEBUG     0   /* LNK-026 phase-offset trace; flip to 1 to re-run (see P4-028) */
#define PHASE_DEBUG_US  250000   /* ~4 Hz */

static KsConfig g_cfg;   /* loaded from NVS in app_main; edited via the web UI */
static volatile uint32_t g_cfg_gen = 0;   /* bumped by web POST /live; clock task re-primes on change (P4-015) */
static SemaphoreHandle_t g_cfg_mutex;     /* ARC-016: guards g_cfg/g_cfg_gen vs the /live writer */

static void clock_out_task(void *arg)
{
    KsTickState ts;  ks_tick_reset(&ts, g_cfg_gen);
    uint32_t    pulses = 0;
    uint32_t    clicks = 0;
    int64_t     last_log = 0;
#if PHASE_DEBUG
    int64_t     last_phase_dbg = 0;     /* throttle the LNK-026 phase-debug log */
#endif
    int64_t     last_led = 0;           /* throttle the WS2812 refresh (P4-018) */
    bool        led_showing = false;    /* is the strip currently lit (to clear once when off) */

    while (1) {
        /* Drive the measurement client (ping/pong -> GhostXForm). Opens its own
         * unicast socket lazily; non-blocking, so it is safe in this 1 ms loop. */
        link_measure_io_poll();

        LinkTimeline tl;
        bool have_session = wifi_link_timeline(&tl) && tl.micros_per_beat > 0;
        LinkGhostXForm xform = link_measurement_current_xform();
        int64_t        t_now = esp_timer_get_time();

        /* Link transport gates the metronome + strip (P4-019): quiet when stopped
         * since the beat keeps advancing (and jumps on a re-origin) while stopped.
         * Falls back to "playing" when the session never reports transport. */
        bool tp_playing = !link_proto_start_stop_seen() || link_proto_playing();

        /* ARC-016: take a coherent snapshot of the live config under the mutex so a
         * concurrent /live patch can't tear a multi-field read; the whole tick then
         * uses cfg (never the shared g_cfg). Held only for the copy — microseconds. */
        KsConfig cfg;
        uint32_t cfg_gen;
        xSemaphoreTake(g_cfg_mutex, portMAX_DELAY);
        cfg     = g_cfg;
        cfg_gen = g_cfg_gen;
        xSemaphoreGive(g_cfg_mutex);

        /* ARC-015: all the decisions (beat basis, the reprime fold, the per-output
         * clock fan-out, the transport action, the metronome-click gating) are the
         * pure ks_tick_step; the loop just executes the returned plan. */
        KsTickInputs in = {
            .have_session = have_session, .xform = xform, .tl = tl, .t_now = t_now,
            .cfg = &cfg, .cfg_gen = cfg_gen, .tp_playing = tp_playing,
            .start_stop_seen = link_proto_start_stop_seen(),
            .playing = link_proto_playing(), .usb_ready = usb_midi_host_ready(),
        };
        KsTickPlan plan = ks_tick_step(&ts, &in);

        /* Execute the clock fan-out: plan.pulses[o] 0xF8 packets on output o's cable. */
        for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
            for (int i = 0; i < plan.pulses[o]; i++) {
                uint8_t pkt[4];
                usb_midi_pack_single(cfg.clock[o].cable, 0xF8, pkt);
                usb_midi_host_send(pkt, 4);
                pulses++;
            }
        }
        /* Transport: fan Start/Stop to every enabled output's cable (P4-008). */
        if (plan.transport != TRANSPORT_NONE) {
            uint8_t status = (plan.transport == TRANSPORT_START) ? 0xFA : 0xFC;
            for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
                if (!cfg.clock[o].enable) continue;
                uint8_t pkt[4];
                usb_midi_pack_single(cfg.clock[o].cable, status, pkt);
                usb_midi_host_send(pkt, 4);
            }
            ESP_LOGI(TAG, "transport %s", plan.transport == TRANSPORT_START ? "START" : "STOP");
        }
        /* Metronome click on the onboard speaker (P4-006). */
        if (plan.click) {
            metronome_audio_click(plan.click_accent);
            clicks++;
            ESP_LOGI(TAG, "metronome %-6s beat %.2f  clicks %lu",
                     plan.click_accent ? "ACCENT" : "click", plan.beats, (unsigned long)clicks);
        }

        int64_t now = esp_timer_get_time();

        /* Visual metronome (P4-018): render the pure bar-position chase from the
         * shared beat onto the WS2812 strip, throttled to ~50 fps so it doesn't
         * steal time from the 1 ms clock loop. Independent of the audio metronome
         * (its own led_enable switch); clears once when disabled or idle. */
        if (now - last_led >= LED_FRAME_US) {
            last_led = now;
            if (cfg.led_enable && plan.active && tp_playing) {   /* off when stopped (see tp_playing above) */
                MetroStripCfg lc = {
                    .beat   = { (uint8_t)(cfg.led_beat_color   >> 16), (uint8_t)(cfg.led_beat_color   >> 8), (uint8_t)cfg.led_beat_color   },
                    .accent = { (uint8_t)(cfg.led_accent_color >> 16), (uint8_t)(cfg.led_accent_color >> 8), (uint8_t)cfg.led_accent_color },
                    .bright = (uint8_t)cfg.led_brightness,
                    .mode   = (uint8_t)cfg.led_mode,
                    .fade   = (uint8_t)cfg.led_fade,
                };
                RGB frame[LED_PIXELS];
                metro_strip_render(plan.beats, (int)KS_TICK_METRO_QUANTUM, LED_PIXELS, &lc, frame);
                ks_led_show(frame, LED_PIXELS);
                led_showing = true;
            } else if (led_showing) {
                ks_led_clear();
                led_showing = false;
            }
        }

        if (now - last_log >= 1000000) {
            last_log = now;
            ESP_LOGI(TAG, "link peers %d  bpm %.2f  phase %s  usb %s  clock TX %lu  play %d",
                     wifi_link_peers(), link_proto_bpm(),
                     plan.locked ? "locked" : "free",
                     usb_midi_host_ready() ? "ready" : "-", pulses,
                     link_proto_playing());
        }

#if PHASE_DEBUG
        /* LNK-026 offset hunt: P4's computed session phase + raw ingredients, for
         * diffing against a ground-truth Link reference. `phase` in [0,quantum);
         * at the reference downbeat it should read ~0. */
        if (plan.active && now - last_phase_dbg >= PHASE_DEBUG_US) {
            last_phase_dbg = now;
            double phase = fmod(plan.beats, KS_TICK_METRO_QUANTUM);
            if (phase < 0) phase += KS_TICK_METRO_QUANTUM;
            int64_t ghost_now = xform.valid ? link_ghost_xform_host_to_ghost(xform, t_now) : 0;
            ESP_LOGI(TAG,
                "PHASE lock=%d phase=%.3f beats=%.3f | xform=%d intercept=%lld ghost_now=%lld"
                " | mpb=%lld beat0=%lld t0=%lld play=%d",
                plan.locked, phase, plan.beats,
                xform.valid, (long long)xform.intercept_us, (long long)ghost_now,
                (long long)tl.micros_per_beat, (long long)tl.beat_origin_micro,
                (long long)tl.time_origin_us, link_proto_playing());
        }
#endif
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

    ks_config_load(&g_cfg);
    ESP_LOGI(TAG, "KitchenSync — Link tempo -> USB-MIDI host clock out (P4-005/007)");
    g_cfg_mutex = xSemaphoreCreateMutex();   /* ARC-016: before the web server / clock task */
    usb_midi_host_start();
    wifi_link_start(g_cfg.wifi_ssid, g_cfg.wifi_pass);
    ks_web_start(&g_cfg, &g_cfg_gen, g_cfg_mutex);
    if (g_cfg.metronome_enable)
        metronome_audio_start(g_cfg.metronome_volume, g_cfg.metronome_voice);   /* codec/I2S only when used */
    ks_led_start(LED_GPIO, LED_PIXELS);   /* WS2812 visual metronome; harmless if nothing wired (P4-018) */
    xTaskCreate(clock_out_task, "clock_out", 4096, NULL, 6, NULL);
}
