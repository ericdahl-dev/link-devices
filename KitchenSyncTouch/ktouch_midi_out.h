#pragma once
// KitchenSync Touch MIDI writer (ESP-016). Emits 24-PPQN clock on the DIN wire.
// DIN-ONLY by design -- see ktouch_midi_out.cpp for why USB-MIDI is deliberately
// absent. Transport (0xFA/0xFC) lands here in Inc2b.
void ktouch_midi_out_begin(int tx_gpio);
