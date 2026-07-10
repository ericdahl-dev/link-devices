#pragma once
// KitchenSync Touch transport mailbox (ESP-016). One-slot, cross-task: the touch
// dispatch (loop) posts a press; the MIDI writer task consumes it exactly once.
// Single output (unlike the P4's per-cable array), so a trivial portMUX slot.
#include "transport_launch.h"   // TransportLaunchIntent

#ifdef __cplusplus
extern "C" {
#endif

void                  ktouch_transport_post(TransportLaunchIntent intent);
TransportLaunchIntent ktouch_transport_take(void);   // consumes; NONE when empty

#ifdef __cplusplus
}
#endif
