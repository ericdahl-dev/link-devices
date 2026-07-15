#pragma once
/* P4-038: the 1 ms clock task's own health, published to /status.
 *
 * KitchenSync ALREADY had this probe -- and threw it away. The counters were windowed
 * and zeroed by a once-a-second serial logger, so the 766 ms clock stall the logic
 * analyzer caught (network load preempting a priority-6 clock task, below lwIP's 18)
 * left no trace anywhere unless somebody happened to have a serial cable attached at
 * that exact second. A worst-case that scrolled past is not a measurement.
 *
 * So it is published, in X32Link's exact WebTickHealth shape and JSON key names
 * (ARC-024), so one fleet script reads every device with one parser:
 *
 *   gap  >> 1000us -> the task was never SCHEDULED. Cause is OUTSIDE the loop:
 *                     preemption, or a flash-cache stall (which freezes BOTH cores,
 *                     so neither priority nor pinning can cure it).
 *   work >> 1000us -> a call INSIDE the tick blocked; w_beats / w_clock say which half.
 *   drop           -> pulses the ticker THREW AWAY on a realign. These leave no gap and
 *                     no burst on the wire, so without this counter a stall long enough
 *                     to trip the cap is undetectable.
 *   core           -> the core the writer REALLY landed on. Report it, never assume it.
 *
 * The clock task never logs: an ESP_LOGx in a 1 ms real-time task is a blocking ~13 ms
 * UART write (P4-033), and a log-per-overrun delays the next tick, which overruns, which
 * logs -- the loop that manufactured 3945 phantom overruns in 40 s on the Touch. The task
 * only ever publishes plain scalars; the web layer reads them.
 */
#include <stdbool.h>
#include "web_status_json.h"   /* WebTickHealth -- shared with X32Link */

#ifdef __cplusplus
extern "C" {
#endif

/* Fills `out` with LIFETIME counters (never reset on read). Returns false only on a
 * NULL argument: KitchenSync's clock task runs from boot, so there is always a real
 * measurement -- unlike X32Link, whose writer exists only when clock-out is enabled. */
bool ks_tick_health(WebTickHealth* out);

// ESP-037: the tempo the clock is actually running (settled value; survives peer loss).
float ks_clock_effective_bpm(void);

#ifdef __cplusplus
}
#endif
