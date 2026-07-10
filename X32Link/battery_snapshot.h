#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// One coherent battery reading (same ARC-001 shape as tempo_snapshot.h).
// Published by the battery task, read by the web /status endpoint.
typedef struct {
    float volts;
    float percent;
    bool  present;  // false = no LiPo BFF found on this poll (or never polled)
} BatterySnapshot;

void battery_snapshot_publish(float volts, float percent, bool present);
void battery_snapshot_read(BatterySnapshot* out);

#ifdef __cplusplus
}
#endif
