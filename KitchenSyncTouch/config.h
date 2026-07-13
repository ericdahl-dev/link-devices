#pragma once
// KitchenSync on Arduino — the CLOCK BOX (Link in -> DIN MIDI clock + transport out).
// NOT the X32Link mixer bridge; see the firmware map at the top of AGENTS.md.
//
// Two boards, one firmware:
//   - Waveshare ESP32-S3-Touch-LCD-1.47 — the product. Transport via the touch UI.
//   - Classic ESP32 DevKit (WROOM-32)   — the ESP-025 bench rig. Transport via two
//     illuminated buttons on a screw-terminal breakout. Same clock engine, same
//     writer, same web UI: what you measure on the bench is what the product runs.
//
// The board flag lives HERE, not in build_opt.h: a #define in a tracked source is
// part of the arduino-cli dependency graph, so it can't go stale in the build
// cache — the exact bug that blanked the X32Link touch build until a --clean.
//
// PICK EXACTLY ONE. Leave the Waveshare line active at HEAD (it is the product);
// comment-swap locally for the bench rig and don't commit the swap.
#define BOARD_WAVESHARE_S3_TOUCH_LCD_147
// #define BOARD_ESP32_DEVKIT

#if defined(BOARD_WAVESHARE_S3_TOUCH_LCD_147) && defined(BOARD_ESP32_DEVKIT)
#error "config.h: pick exactly ONE board — both are defined"
#endif
#if !defined(BOARD_WAVESHARE_S3_TOUCH_LCD_147) && !defined(BOARD_ESP32_DEVKIT)
#error "config.h: pick exactly ONE board — neither is defined"
#endif

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
