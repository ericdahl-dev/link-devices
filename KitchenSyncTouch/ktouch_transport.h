#pragma once
// KitchenSync Touch transport mailbox (ESP-016). One-slot, cross-task: the touch
// dispatch (loop) posts a press; the MIDI writer task consumes it exactly once.
// Single output (unlike the P4's per-cable array), so a trivial portMUX slot.
#include "transport_launch.h"   // TransportLaunchIntent
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void                  ktouch_transport_post(TransportLaunchIntent intent);
TransportLaunchIntent ktouch_transport_take(void);   // consumes; NONE when empty

// The writer publishes the current launch state (TL_STOPPED/ARMED/RUNNING) each
// tick; the display reads it to colour the toggle and pick the tap direction.
void ktouch_transport_publish_state(int launch_state);
int  ktouch_transport_state(void);

// ESP-025: realign is armed while RUNNING, so it cannot be expressed as a launch
// state without lying about whether the device is playing. The lit REALIGN button
// blinks on this; without it a quantized press looks dead for a whole bar.
void ktouch_transport_publish_realign(bool armed);
bool ktouch_transport_realign_armed(void);

#ifdef __cplusplus
}
#endif
