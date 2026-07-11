#pragma once
// KitchenSync Touch MIDI writer (ESP-016). Emits 24-PPQN clock on the DIN wire.
// DIN-ONLY by design -- see ktouch_midi_out.cpp for why USB-MIDI is deliberately
// absent. Transport (0xFA/0xFC) lands here in Inc2b.
void ktouch_midi_out_begin(int tx_gpio);

// ESP-018 tick health, surfaced in /status. The clock task never logs (an ESP_LOGx in a
// 1ms real-time task is a blocking UART write -- that WAS the bug on the P4, P4-033), so
// it publishes and the web layer reads.
//   gap  >> 1ms -> the task was not scheduled (starved, or a flash-cache stall, which
//                  freezes BOTH cores regardless of pinning or priority)
//   work >> 1ms -> something inside the tick blocked
//   dropped     -> pulses the scheduler THREW AWAY on a realign past MAX_BURST. Until
//                  this counter existed, a stall long enough to trip it left no burst
//                  and no gap on the wire -- it was undetectable.
#include <stdint.h>
uint32_t ktouch_midi_max_gap(void);
uint32_t ktouch_midi_max_work(void);
uint32_t ktouch_midi_overruns(void);
uint32_t ktouch_midi_bursts(void);
uint32_t ktouch_midi_dropped(void);
int      ktouch_midi_core(void);    // which core the writer really runs on
uint32_t ktouch_midi_w_beats(void); // worst-tick: time inside tempo_source_beats_now()
uint32_t ktouch_midi_w_clock(void); // worst-tick: clock scheduling + DIN writes
uint32_t ktouch_midi_w_tport(void); // worst-tick: transport step
