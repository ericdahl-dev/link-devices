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

#include "fw_version.h"       /* LNK-038: shared FW_VERSION / FW_BUILD identity */
#include "wifi_link.h"
#include "usb_midi_host.h"
#include "ks_web.h"
#include "ks_config.h"
#include "ks_config_nvs.h"
#include "beat_source.h"      /* basis selector: session phase vs free-run (ARC-007) */
#include "clock_ticker.h"
#include "metronome.h"
#include "metronome_audio.h"
#include "i2s_audio_bus.h"     /* shared I2S_NUM_0 + ES8311 codec owner (P4-020) */
#include "follow_beat_io.h"    /* mic capture task (P4-020) */
#include "metro_strip.h"     /* pure WS2812 bar-position chase (P4-018) */
#include "ks_led.h"       /* WS2812 strip glue (RMT) */
#include "usb_midi_pack.h"
#include "usb_midi_batch.h"   /* P4-034: one bulk transfer per tick, not one per output */
#include "midi_uart_out.h"   /* ESP-015: DIN MIDI out mirror + downbeat strobe */
#include "transport.h"        /* Link play/stop -> MIDI Start/Stop (P4-008) */
#include "link_protocol.h"
#include "link_measurement.h"   /* GhostXForm accessor (link_measurement_current_xform) */
#include "link_measure_io.h"    /* unicast ping/pong measurement client */
#include "clock_output.h"       /* per-output division + phase-nudge (P4-010) */
#include "ks_tick.h"
#include "master_clock.h"       /* P4-040: internal/tap tempo source, solo fallback */
#include "transport_intent.h"   /* ESP-011: web UI launch presses */            /* ARC-015: pure clock-tick orchestration */

static const char *TAG = "kitchensync";

/* Tick-loop policy constants moved to ks_tick.h (KS_TICK_*, ARC-015). */
#define LED_GPIO        2    /* WS2812 data pin (P4-018); external strip + 5V/GND */
#define LED_PIXELS      METRO_STRIP_PIXELS

/* ESP-015 hardware MIDI OUT + analyzer strobe. CANDIDATE pins -- confirm they are
 * broken out on the P4-NANO header and clear of the MIPI/SD mux before wiring.
 * Forbidden (verified): 7-13 (I2C/I2S), 14-19 + 54 (C6 SDIO -> WiFi), 37/38
 * (console UART0), 53 (amp). GPIO20/21 are matrix-routed, any free pin works. */
#define MIDI_TX_GPIO     20   /* UART1 TX, 31250 8N1 -- DIN MIDI out (+2 resistors) */
#define MIDI_STROBE_GPIO 21   /* one pulse per bar; analyzer triggers here (ESP-011) */
#define MIDI_MIRROR_OUT  0    /* which output's stream the wire carries (realtime
                              * bytes are cable-agnostic, so mirror exactly one) */
#define STANDBY_PERIOD_US 2000000   /* one standby breath = 2s (ESP-009) */
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

/* P4-033: the clock task must NEVER call ESP_LOGx.
 *
 * The console is a BLOCKING UART write: at 115200 baud one ~110-char status line
 * costs 7.5-12 ms. Measured on the bench with a per-stage tick probe -- every other
 * stage in the 1 ms loop was single-digit microseconds while the log alone was
 * 7537 us. That stalls the clock generator, and the catch-up is capped by MAX_BURST,
 * so pulses are DROPPED, not merely delayed. The logic analyzer saw it directly:
 * ~100 ms gaps in the 0xF8 stream followed by bytes emitted 0.32 ms apart.
 *
 * Worst of all was the per-transition transport log -- it fired at the downbeat, the
 * one moment a stall is guaranteed to be audible.
 *
 * So the clock task publishes plain scalars here and a low-priority task does the
 * printing. A torn read just prints a slightly stale number; nothing depends on it. */
static volatile struct {
    int      peers;
    float    bpm;
    bool     locked, usb, playing;
    uint32_t pulses, clicks;
    uint32_t usb_dropped;         /* P4-034: USB packets binned by a busy endpoint */
    int64_t  max_gap, max_work;   /* tick-health probe; reset by the logger */
    uint32_t overruns;
} s_stat;

/* Priority 2 — the lowest thing running, and deliberately so. It logs (a blocking
 * UART write, ~10 ms: fatal in the 1 ms clock task, harmless here, P4-033) and, since
 * ARC-022, it also performs the debounced NVS write for live config edits. A flash
 * write suspends the cache and freezes BOTH cores, so it cannot be made safe by
 * priority — it can only be kept off the clock task and made rare. It polls at 250 ms
 * so a settled edit is persisted promptly, and logs every fourth pass to keep the
 * once-a-second serial cadence. */
static void status_task(void *arg)
{
    (void)arg;
    int pass = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(250));
        ks_web_config_persist_tick();   /* ARC-022: no-op unless /live edits have settled */
        if (++pass < 4) continue;
        pass = 0;
        ESP_LOGI(TAG, "link peers %d  bpm %.2f  phase %s  usb %s  clock TX %lu  play %d  usbdrop %lu",
                 s_stat.peers, s_stat.bpm, s_stat.locked ? "locked" : "free",
                 s_stat.usb ? "ready" : "-", (unsigned long)s_stat.pulses, s_stat.playing,
                 (unsigned long)s_stat.usb_dropped);
        if (s_stat.overruns) {
            ESP_LOGW(TAG, "tick health: overruns=%lu max_gap=%lldus max_work=%lldus",
                     (unsigned long)s_stat.overruns,
                     (long long)s_stat.max_gap, (long long)s_stat.max_work);
        }
        s_stat.overruns = 0; s_stat.max_gap = 0; s_stat.max_work = 0;
    }
}

static void clock_out_task(void *arg)
{
    KsTickState ts;  ks_tick_reset(&ts, g_cfg_gen);
    MasterClock mclock; master_clock_reset(&mclock);   /* P4-040 */
    uint32_t    pulses = 0;
    uint32_t    clicks = 0;
    int64_t     last_log = 0;
#if PHASE_DEBUG
    int64_t     last_phase_dbg = 0;     /* throttle the LNK-026 phase-debug log */
#endif
    int64_t     last_led = 0;           /* throttle the WS2812 refresh (P4-018) */
    bool        led_showing = false;    /* is the strip currently lit (to clear once when off) */

    /* Tick-overrun probe: the analyzer caught the DIN clock stalling ~100 ms and then
     * emitting a capped catch-up burst (dropping pulses). Everything below shares this
     * one 1 ms task -- socket I/O, the Link reads, I2S metronome, WS2812 RMT -- so any
     * blocking call stalls the clock. `gap` (previous tick's end -> this tick's start)
     * separates the two possible causes: a large gap means the task was never scheduled
     * (starved / flash-cache stall), a large `work` means one of OUR calls blocked. */
    int64_t prev_end = 0;

    /* P4-034: persists ACROSS ticks -- if the USB endpoint is busy the batch is held
     * and retried next tick rather than dropped. */
    UsbMidiBatch usb_batch = {0};

    while (1) {
        int64_t tk0 = esp_timer_get_time();
        int64_t gap = prev_end ? (tk0 - prev_end) : 0;

        /* Drive the measurement client (ping/pong -> GhostXForm). Opens its own
         * unicast socket lazily; non-blocking, so it is safe in this 1 ms loop. */
        link_measure_io_poll();

        LinkTimeline link_tl;
        bool link_have_session = wifi_link_timeline(&link_tl) && link_tl.micros_per_beat > 0;
        LinkGhostXForm link_xform = link_measurement_current_xform();
        int64_t        t_now = esp_timer_get_time();

        /* P4-040: arbiter picks Link vs internal/tap. Always defers to Link
         * when a peer is present (this device never broadcasts, so there is
         * no competing session to merge); solo, falls back to the internal
         * tempo, seeded from whatever Link last showed. */
        MasterArbiterOut mc_out = master_clock_arbiter(&mclock, wifi_link_peers(),
                                                        link_have_session, link_tl, link_xform);
        bool           have_session = mc_out.have_session;
        LinkTimeline   tl           = mc_out.tl;
        LinkGhostXForm xform        = mc_out.xform;

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
        transport_intent_take(in.launch);   /* ESP-011: one press, one action */
        KsTickPlan plan = ks_tick_step(&ts, &in);
        {   /* ESP-011: surface arming state to the web UI. */
            int ls[KS_CLOCK_OUTPUTS];
            for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) ls[o] = (int)plan.launch_state[o];
            ks_web_publish_launch(ls);
        }

        /* One pulse per bar to the analyzer trigger line (ESP-015); high for this
         * tick, low the next -- a clean rising edge on the Link bar boundary. */
        midi_uart_out_strobe(plan.downbeat);

        /* Execute the clock fan-out: plan.pulses[o] 0xF8 packets on output o's cable.
         *
         * P4-034: USB events are STAGED into one batch and submitted as a single bulk
         * transfer at the end of the tick. Never call usb_midi_host_send() per output:
         * it holds one in-flight transfer (~1 ms, a whole USB frame) and drops anything
         * offered while busy, so the old per-output calls delivered only the FIRST
         * output and silently binned the rest. Batching also puts every output's clock
         * in the same USB frame, so they stay aligned to each other. */
        bool usb_ready = in.usb_ready;   /* gate USB sends; the DIN wire emits regardless */
        for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
            for (int i = 0; i < plan.pulses[o]; i++) {
                /* DIN MIDI out (ESP-015): the wire clocks whether or not a USB host
                 * is attached -- emit first, from the same code point, so its timing
                 * tracks the USB emit when a host is present. */
                if (o == MIDI_MIRROR_OUT) midi_uart_out_byte(0xF8);
                if (usb_ready) {
                    uint8_t pkt[4];
                    usb_midi_pack_single(cfg.clock[o].cable, 0xF8, pkt);
                    usb_midi_batch_add(&usb_batch, pkt);
                }
                pulses++;
            }
        }
        /* Transport, per output (P4-008 + ESP-011): each output runs its own
         * Start/Stop, so gear can be armed onto different bars. */
        for (int o = 0; o < KS_CLOCK_OUTPUTS; o++) {
            if (plan.transport[o] == TRANSPORT_NONE) continue;
            uint8_t status = (plan.transport[o] == TRANSPORT_START) ? 0xFA : 0xFC;
            if (o == MIDI_MIRROR_OUT) midi_uart_out_byte(status);   /* DIN, always (ESP-015) */
            if (usb_ready) {
                uint8_t pkt[4];
                usb_midi_pack_single(cfg.clock[o].cable, status, pkt);
                usb_midi_batch_add(&usb_batch, pkt);   /* P4-034: staged, not submitted */
            }
            /* No ESP_LOG here (P4-033): this runs ON the bar line, so a blocking
             * ~10 ms console write would stall the clock at the exact instant the
             * downbeat lands. The web UI's transport pill already shows the state. */
        }

        /* P4-034: ONE bulk transfer for the whole tick. If the endpoint is still busy
         * the batch is KEPT and retried next tick -- 16 events of headroom against
         * ~0.27 events/ms of real traffic, so a one-tick stall is absorbed, not lost. */
        if (usb_ready && usb_batch.len > 0) {
            if (usb_midi_host_send(usb_batch.buf, usb_batch.len)) usb_midi_batch_reset(&usb_batch);
        }
        /* Metronome click on the onboard speaker (P4-006). Counted, not logged
         * (P4-033): this fires every beat, so a per-click console write stalled the
         * clock 2-3x a second. status_task reports the running count instead. */
        if (plan.click) {
            metronome_audio_click(plan.click_accent);
            clicks++;
        }

        int64_t now = esp_timer_get_time();

        /* Visual metronome (P4-018): render the pure bar-position chase from the
         * shared beat onto the WS2812 strip, throttled to ~50 fps so it doesn't
         * steal time from the 1 ms clock loop. Independent of the audio metronome
         * (its own led_enable switch); clears once when disabled or idle. */
        if (now - last_led >= LED_FRAME_US) {
            last_led = now;
            if (cfg.led_enable && plan.active) {
                MetroStripCfg lc = {
                    .beat   = { (uint8_t)(cfg.led_beat_color   >> 16), (uint8_t)(cfg.led_beat_color   >> 8), (uint8_t)cfg.led_beat_color   },
                    .accent = { (uint8_t)(cfg.led_accent_color >> 16), (uint8_t)(cfg.led_accent_color >> 8), (uint8_t)cfg.led_accent_color },
                    .bright = (uint8_t)cfg.led_brightness,
                    .mode   = (uint8_t)cfg.led_mode,
                    .fade   = (uint8_t)cfg.led_fade,
                };
                RGB frame[LED_PIXELS];
                if (plan.standby) {
                    /* Joined but waiting for transport (ESP-009): breathe instead of
                     * going dark, which is indistinguishable from a dead board. The
                     * phase is wall-clock, not beat-derived -- in standby there is no
                     * beat to derive it from. */
                    double ph = (double)(now % STANDBY_PERIOD_US) / (double)STANDBY_PERIOD_US;
                    metro_strip_standby(ph, LED_PIXELS, &lc, frame);
                } else {
                    metro_strip_render(plan.beats, (int)KS_TICK_METRO_QUANTUM, LED_PIXELS, &lc, frame);
                }
                ks_led_show(frame, LED_PIXELS);
                led_showing = true;
            } else if (led_showing) {
                ks_led_clear();
                led_showing = false;
            }
        }

        /* Publish, don't print (P4-033). Plain stores, ~microseconds; status_task
         * owns the console. This is what used to be a 7.5 ms blocking UART write. */
        if (now - last_log >= 1000000) {
            last_log = now;
            s_stat.peers   = wifi_link_peers();
            /* P4-039: plan.bpm/plan.playing hold the settled/last-known values —
             * link_proto_bpm()/link_proto_playing() zero out on peer loss even
             * while the clock (locked to the settled timeline) keeps running. */
            s_stat.bpm     = plan.bpm;
            s_stat.locked  = plan.locked;
            s_stat.usb     = usb_midi_host_ready();
            s_stat.playing = plan.playing;
            s_stat.pulses  = pulses;
            s_stat.clicks  = clicks;
            s_stat.usb_dropped = usb_midi_host_dropped() + usb_batch.dropped;
        }

        /* Name the stall. gap >> 1ms => the task wasn't scheduled (starvation /
         * flash-cache stall) and the culprit is OUTSIDE this loop; work >> 1ms => a
         * call in here blocked, and the per-stage maxima say which.
         *
         * Reported at most once a second, NOT per overrun: an ESP_LOGW is a blocking
         * ~13ms UART write at 115200, so logging every overrun delays the next tick
         * and manufactures the very overrun it reports -- a feedback loop that fired
         * 99x/sec on the first run and drowned the real signal. Accumulate, then
         * report. prev_end is stamped after the log so the probe's own cost is never
         * misread as a system stall. */
        /* Tick health, published for status_task. Kept permanently: this probe is what
         * caught the logging stall, and it costs two esp_timer reads per tick. A
         * healthy clock reports no overruns at all. */
        int64_t tk1  = esp_timer_get_time();
        int64_t work = tk1 - tk0;
        if (gap  > s_stat.max_gap)  s_stat.max_gap  = gap;
        if (work > s_stat.max_work) s_stat.max_work = work;
        if (gap > 5000 || work > 5000) s_stat.overruns++;
        prev_end = tk1;

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
    /* fw/built as %s args, not pasted into the format — an injected FW_VERSION
     * containing '%' must never become a conversion specifier. */
    ESP_LOGI(TAG, "KitchenSync fw:%s built:%s — Link tempo -> USB-MIDI host clock out (P4-005/007)",
             FW_VERSION, FW_BUILD);
    g_cfg_mutex = xSemaphoreCreateMutex();   /* ARC-016: before the web server / clock task */
    usb_midi_host_start();
    WifiCred creds[KS_WIFI_SLOTS];                        /* ESP-013: try each saved network in turn */
    int ncreds = ks_config_wifi_slots(&g_cfg, creds);     /* empty slots dropped here, not in the policy */
    wifi_link_start(creds, ncreds);
    ks_web_start(&g_cfg, &g_cfg_gen, g_cfg_mutex);
    if (g_cfg.metronome_enable || g_cfg.follow_beat_enable)
        audio_bus_init(AUDIO_BUS_SAMPLE_RATE);   /* shared I2S_NUM_0 + ES8311 codec owner (P4-020) -- once, before either consumer */
    if (g_cfg.metronome_enable)
        metronome_audio_start(g_cfg.metronome_volume, g_cfg.metronome_voice);   /* codec/I2S only when used */
    if (g_cfg.follow_beat_enable)
        follow_beat_io_start();   /* mic-based tempo detection, display-only v1 (P4-020) */
    ks_led_start(LED_GPIO, LED_PIXELS);   /* WS2812 visual metronome; harmless if nothing wired (P4-018) */
    midi_uart_out_start(MIDI_TX_GPIO, MIDI_STROBE_GPIO);   /* ESP-015: DIN MIDI out + analyzer strobe */
    xTaskCreate(clock_out_task, "clock_out", 4096, NULL, 6, NULL);
    /* P4-033: the console lives HERE, not in clock_out. Low priority (2) so a blocking
     * UART write can never preempt the clock generator. */
    xTaskCreate(status_task, "status", 3072, NULL, 2, NULL);
}
