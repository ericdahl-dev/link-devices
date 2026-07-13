#pragma once
// ESP-025: lamp state for the two illuminated transport buttons. Pure — takes the
// launch state and a clock, returns whether the lamp is lit. No Arduino, no GPIO.
//
// This exists because a quantized button is INVISIBLE without it. Press Play mid-bar
// and nothing happens for up to a whole bar; every user's instinct is to press again.
// The blink is what says "heard you, waiting for the bar line".
#include "transport_launch.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fast enough to read as "pending" rather than as a slow status heartbeat, and far
// off the beat LED's per-beat flash so the two can't be confused on the bench.
#define TRANSPORT_LED_BLINK_MS 120

// Transport button: dark when stopped, blinking while armed, solid while running.
bool transport_led_on(TransportLaunchState state, uint32_t now_ms);

// Realign button: blinking while a realign is armed, dark otherwise. It is never
// solid — realign is a momentary action, not a mode you sit in.
bool realign_led_on(bool realign_armed, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
