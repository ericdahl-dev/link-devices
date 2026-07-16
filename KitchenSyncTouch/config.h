#pragma once
// KitchenSync on Arduino — the CLOCK BOX (Link in -> DIN MIDI clock + transport out).
// NOT the X32Link mixer bridge; see the firmware map at the top of AGENTS.md.
//
// Three boards, one firmware:
//   - Waveshare ESP32-S3-Touch-LCD-1.47 — the product. Transport via the touch UI.
//   - Classic ESP32 DevKit (WROOM-32)   — the ESP-025 bench rig. Transport via two
//     illuminated buttons on a screw-terminal breakout. Same clock engine, same
//     writer, same web UI: what you measure on the bench is what the product runs.
//   - ESP32-S3 Super Mini (ESP-036)     — HEADLESS. No screen, no buttons (yet):
//     transport is driven entirely from the iOS app over /transport + /live. This is
//     the small, cheap clock-box variant — a bare S3 + a DIN/TRS MIDI jack. The DIN
//     MIDI TX lands on GPIO11 (din_midi_out.h), the same free header pin the Waveshare
//     uses. Buttons are EXPECTED here eventually: the HAS_BUTTONS path already exists
//     (the DevKit uses it), so adding them later means defining HAS_BUTTONS and giving
//     the button/LED pins S3-safe GPIOs — the DevKit's 25/26 are classic-ESP32 pins and
//     do NOT carry over (26-32 are the SPI flash bus on an S3 module).
//
// The board flag lives HERE, not in build_opt.h: a #define in a tracked source is
// part of the arduino-cli dependency graph, so it can't go stale in the build
// cache — the exact bug that blanked the X32Link touch build until a --clean.
//
// PICK EXACTLY ONE. Leave the Waveshare line active at HEAD (it is the product);
// comment-swap locally for the bench rig / Super Mini and don't commit the swap.
#define BOARD_WAVESHARE_S3_TOUCH_LCD_147
// #define BOARD_ESP32_DEVKIT
// #define BOARD_S3_SUPERMINI

// EXACTLY ONE board. `defined()` summed inside an #if is well-defined (it is NOT in a
// macro body), so this reads as "count the board flags; there must be one".
#if (defined(BOARD_WAVESHARE_S3_TOUCH_LCD_147) \
   + defined(BOARD_ESP32_DEVKIT) \
   + defined(BOARD_S3_SUPERMINI)) != 1
#error "config.h: pick EXACTLY ONE board flag"
#endif

// ---- ESP-030: what is ACTUALLY WIRED to this board -------------------------
//
// These drive the KsCaps this device reports, and a device must not report hardware it
// does not have: emitting `led:false` with no strip attached is the same class of lie
// as ESP-028's `sync:1` over a wire that had been dead for 138 seconds. A client would
// dutifully draw an LED section for a board that cannot light anything.
//
// Capabilities belong to THIS BUILD, not to the product name. "The Touch has no LED" is
// wrong — the truth is "no strip is wired to this one YET". Solder one on, flip
// KSTOUCH_HAS_LED to 1, and /config.json starts emitting led_*, and the iOS app's
// settings screen grows an LED section — with NO app change at all. That is the whole
// point of declaring capability instead of hardcoding a product identity.
#define KSTOUCH_HAS_METRONOME   0   // no speaker fitted (the P4 has an ES8311; this doesn't)
#define KSTOUCH_HAS_LED         0   // no WS2812 strip wired — set to 1 when one is
#define KSTOUCH_HAS_FOLLOWBEAT  0   // no mic fitted
#define KSTOUCH_CLOCK_OUTPUTS   1   // one DIN jack. The clock array's LENGTH is this.

// Link timing — the receive-only Link stack we reuse from X32Link expects these.
#define LINK_DEFAULT_BPM      120.0f
#define LINK_BPM_THRESHOLD    0.5f
#define LINK_POLL_MS          50
#define LINK_SEND_INTERVAL_MS 500
#define LINK_REFRESH_BARS     1

// MIDI clock: 24 PPQN protocol constant (drives the clock generator, Inc 1b). The
// rest are consumed by the shared midi_clock.cpp's MIDI-IN ring, which is dead code
// here (Link-only) but still compiled — keep the constants so it builds.
#define MCK_PPQN              24
#define MCK_BPM_THRESHOLD     1.5f
#define MCK_POLL_MS           10
#define MCK_CLOCK_WINDOW      48
#define MCK_CLOCK_TIMEOUT_MS  2000

// KitchenSync Touch defaults — a MIDI product, NO mixer/OSC/model/fx anything.
#define DEFAULT_QUANTUM_BEATS    4   // 4 beats = 1 bar (4/4); web UI edits in bars
#define DEFAULT_CLOCK_ENABLE     1   // clock on by default (unlike X32Link's mixer product)
#define DEFAULT_TRANSPORT_ENABLE 1
#define DEFAULT_PLAY_ON_RELEASE  0   // 0 = fire on touch, 1 = on release
#define DEFAULT_NUDGE_MBEATS     0   // DIN clock phase trim, millibeats
#define DEFAULT_BRIGHTNESS       80  // LCD backlight percent

// The board flag implies the on-device touch UI (LNK-014/015).
// ESP-025: the DevKit has no display — its transport surface is two lit buttons.
// Both boards post the same TransportLaunchIntent into the same ktouch_transport
// mailbox, so the writer neither knows nor cares which one is fitted.
#if defined(BOARD_ESP32_DEVKIT) && !defined(HAS_BUTTONS)
#define HAS_BUTTONS
#endif

#if defined(BOARD_WAVESHARE_S3_TOUCH_LCD_147) && !defined(HAS_TOUCH_DISPLAY)
#define HAS_TOUCH_DISPLAY
#endif

// ESP-043: the S3 Super Mini has ONE onboard addressable RGB (WS2812 on GPIO48, per the
// Arduino esp32s3 variant's PIN_RGB_LED). A headless clock box has no screen, so drive it
// as a visual beat/heartbeat -- the only local feedback the board can give. NOT enabled on
// the Waveshare board (GPIO48 is spoken for by the panel there) or the DevKit.
#if defined(BOARD_S3_SUPERMINI) && !defined(HAS_STATUS_RGB)
#define HAS_STATUS_RGB
#define STATUS_RGB_GPIO 48
#endif
