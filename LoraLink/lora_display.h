#pragma once

void lora_display_begin();
void lora_display_show_sender(int peers, float bpm, bool link_active);
void lora_display_show_receiver(float bpm, bool stale);
