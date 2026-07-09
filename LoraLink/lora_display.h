#pragma once

void lora_display_begin();
void lora_display_show_sender(int peers, float bpm, bool link_active);
// rx_blink toggles on every packet received (BPM or NO_SESSION alike), so
// there's a visible heartbeat even while the BPM value itself isn't changing.
void lora_display_show_receiver(float bpm, bool stale, bool rx_blink);
