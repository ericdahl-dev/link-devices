CC      = gcc
CFLAGS  = -Wall -Wextra -g \
           -I../X32Link \
           -I../lib/unity/src

UNITY   = ../lib/unity/src/unity.c
PROTO   = ../X32Link/link_protocol.c
MEAS    = ../X32Link/link_measurement.c
PHASE   = ../X32Link/link_phase.c
LEDPHASE = ../X32Link/led_phase.c
WIFIBLINK = ../X32Link/wifi_down_blink.c
TRACKER = ../X32Link/bpm_tracker.c
CFG     = ../X32Link/app_config.c
OSC     = ../X32Link/osc_out.c
MIDICALC = ../X32Link/midi_bpm_calc.c
PUB     = ../X32Link/bpm_publisher.c
OSCIN   = ../X32Link/osc_in.c
STATUSJSON = ../X32Link/web_status_json.c
AXS     = ../X32Link/axs5106l.c
TOUCHUI = ../X32Link/touch_ui.c
SNAP    = ../X32Link/tempo_snapshot.c
BAR     = ../X32Link/bar.c
MIDIOUT = ../X32Link/midi_clock_out.c
CLOCKTICK = ../X32Link/clock_ticker.c
METRO   = ../X32Link/metronome.c
BEATSYNTH = ../X32Link/beat_synth.c
SESSION = ../X32Link/link_measurement_session.c
SESSIONTL = ../X32Link/session_timeline.c
PUMP = ../X32Link/link_measure_pump.c
USBMIDIPACK = ../X32Link/usb_midi_pack.c
BEATCLOCK = ../X32Link/beat_clock.c
BEATSOURCE = ../X32Link/beat_source.c
P4STATUS = ../KitchenSync/main/ks_status.c
P4CONFIG = ../KitchenSync/main/ks_config.c
P4FORM  = ../KitchenSync/main/ks_form.c
P4HOST  = ../KitchenSync/main/ks_hostname.c
UICHROME = ../X32Link/ui_chrome.c
KSTICK  = ../KitchenSync/main/ks_tick.c
MIDICLOCKIN = ../KitchenSync/main/midi_clock_in.c
FOLLOWBEAT = ../KitchenSync/main/follow_beat.c
WIFICONNPOLICY = ../X32Link/wifi_conn_policy.c
TRANSPORT = ../X32Link/transport.c
TLAUNCH = ../X32Link/transport_launch.c
CLOCKOUTPUT = ../X32Link/clock_output.c
MVOICE = ../X32Link/metronome_voice.c
SWING   = ../X32Link/swing.c
METROSTRIP = ../X32Link/metro_strip.c
LORAPKT    = ../LoraLink/lora_bpm_packet.c
LORAFRESH  = ../LoraLink/lora_freshness.c
BATTGAUGE = ../X32Link/battery_gauge.c
BEATSTAMP = ../LinkAudioPoC/main/beat_stamper.c

# Every test depends on every header. Coarse on purpose: the rules below list
# only .c prerequisites, so editing a header used to leave a stale binary that
# passed locally while CI, building fresh, failed -- exactly how a broken
# wifi_conn_policy test reached PR #47 (WIFI_CONN_TIMEOUT_US 30s -> 45s, tests
# still asserting the old boundary). The whole suite builds in seconds, so
# rebuilding all of it on any header change is the right trade.
# (-MMD is no help here: these rules compile many .c straight to an executable,
# so each source overwrites the same .d and only the last one's headers survive.)
# Recipes use $(filter %.c,$^) so the header prerequisites never reach gcc.
HEADERS = $(wildcard ../X32Link/*.h ../KitchenSync/main/*.h ../LoraLink/*.h \
                     ../LinkAudioPoC/main/*.h ../lib/unity/src/*.h)

.PHONY: all clean

all: test_bpm_tracker test_link_protocol test_link_measurement test_link_phase test_led_phase test_wifi_down_blink test_app_config test_osc_out test_midi_bpm_calc test_bpm_publisher test_osc_in test_web_status_json test_touch_ui test_tempo_snapshot test_bar test_midi_clock_out test_clock_ticker test_beat_synth test_link_measurement_session test_session_timeline test_link_measure_pump test_ks_tick test_follow_beat test_usb_midi_pack test_beat_clock test_ks_status test_ks_config test_ks_form test_metronome test_transport test_clock_output test_metronome_voice test_beat_source test_swing test_midi_clock_in test_metro_strip test_wifi_conn_policy test_lora_bpm_packet test_lora_freshness test_battery_gauge test_beat_stamper test_transport_launch test_ks_hostname test_ui_chrome
	./test_bpm_tracker
	./test_link_protocol
	./test_link_measurement
	./test_link_phase
	./test_led_phase
	./test_wifi_down_blink
	./test_app_config
	./test_osc_out
	./test_midi_bpm_calc
	./test_bpm_publisher
	./test_osc_in
	./test_web_status_json
	./test_touch_ui
	./test_tempo_snapshot
	./test_bar
	./test_midi_clock_out
	./test_clock_ticker
	./test_beat_synth
	./test_link_measurement_session
	./test_session_timeline
	./test_link_measure_pump
	./test_ks_tick
	./test_follow_beat
	./test_usb_midi_pack
	./test_beat_clock
	./test_beat_source
	./test_ks_status
	./test_ks_config
	./test_ks_form
	./test_transport
	./test_clock_output
	./test_metronome_voice
	./test_swing
	./test_midi_clock_in
	./test_metro_strip
	./test_wifi_conn_policy
	./test_metronome
	./test_lora_bpm_packet
	./test_lora_freshness
	./test_battery_gauge
	./test_beat_stamper
	./test_transport_launch
	./test_ks_hostname
	./test_ui_chrome
	./check_docs.sh

test_bpm_tracker: test_bpm_tracker.c $(TRACKER) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_link_protocol: test_link_protocol.c $(PROTO) $(SESSIONTL) $(PHASE) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_link_measurement: test_link_measurement.c $(MEAS) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_link_phase: test_link_phase.c $(PHASE) $(PROTO) $(SESSIONTL) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_led_phase: test_led_phase.c $(LEDPHASE) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_wifi_down_blink: test_wifi_down_blink.c $(WIFIBLINK) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_app_config: test_app_config.c $(CFG) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_osc_out: test_osc_out.c $(OSC) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_midi_bpm_calc: test_midi_bpm_calc.c $(MIDICALC) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_bpm_publisher: test_bpm_publisher.c $(PUB) $(BAR) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_osc_in: test_osc_in.c $(OSCIN) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_web_status_json: test_web_status_json.c $(STATUSJSON) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_axs5106l: test_axs5106l.c $(AXS) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_touch_ui: test_touch_ui.c $(TOUCHUI) $(CFG) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_tempo_snapshot: test_tempo_snapshot.c $(SNAP) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_bar: test_bar.c $(BAR) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_midi_clock_out: test_midi_clock_out.c $(MIDIOUT) $(CLOCKTICK) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_clock_ticker: test_clock_ticker.c $(CLOCKTICK) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_beat_synth: test_beat_synth.c $(BEATSYNTH) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_link_measurement_session: test_link_measurement_session.c $(SESSION) $(PHASE) $(PROTO) $(SESSIONTL) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_session_timeline: test_session_timeline.c $(SESSIONTL) $(PHASE) $(PROTO) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_link_measure_pump: test_link_measure_pump.c $(PUMP) $(MEAS) $(SESSION) $(SESSIONTL) $(PHASE) $(PROTO) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_usb_midi_pack: test_usb_midi_pack.c $(USBMIDIPACK) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_beat_clock: test_beat_clock.c $(BEATCLOCK) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_beat_source: test_beat_source.c $(BEATSOURCE) $(BEATCLOCK) $(PHASE) $(PROTO) $(MEAS) $(SESSIONTL) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_ks_status: test_ks_status.c $(P4STATUS) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@

test_battery_gauge: test_battery_gauge.c $(BATTGAUGE) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

clean:
	rm -f test_bpm_tracker test_link_protocol test_link_measurement test_link_phase test_led_phase test_wifi_down_blink test_app_config test_osc_out test_midi_bpm_calc test_bpm_publisher test_osc_in test_web_status_json test_axs5106l test_touch_ui test_tempo_snapshot test_bar

test_ui_chrome: test_ui_chrome.c $(UICHROME) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_ks_hostname: test_ks_hostname.c $(P4HOST) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@

test_ks_config: test_ks_config.c $(P4CONFIG) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@

test_ks_form: test_ks_form.c $(P4FORM) $(P4CONFIG) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@

test_midi_clock_in: test_midi_clock_in.c $(MIDICLOCKIN) $(MIDICALC) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@ -lm

test_metronome: test_metronome.c $(METRO) $(CLOCKTICK) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_transport: test_transport.c $(TRANSPORT) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_transport_launch: test_transport_launch.c $(TLAUNCH) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_clock_output: test_clock_output.c $(CLOCKOUTPUT) $(CLOCKTICK) $(SWING) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_metronome_voice: test_metronome_voice.c $(MVOICE) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_swing: test_swing.c $(SWING) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_metro_strip: test_metro_strip.c $(METROSTRIP) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lm

test_wifi_conn_policy: test_wifi_conn_policy.c $(WIFICONNPOLICY) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test_ks_tick: test_ks_tick.c $(KSTICK) $(TLAUNCH) $(BEATSOURCE) $(BEATCLOCK) $(CLOCKOUTPUT) $(CLOCKTICK) $(SWING) $(METRO) $(TRANSPORT) $(PHASE) $(PROTO) $(SESSIONTL) $(MEAS) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@ -lm

test_lora_bpm_packet: test_lora_bpm_packet.c $(LORAPKT) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../LoraLink $(filter %.c,$^) -o $@

test_lora_freshness: test_lora_freshness.c $(LORAFRESH) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../LoraLink $(filter %.c,$^) -o $@

test_follow_beat: test_follow_beat.c $(FOLLOWBEAT) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../KitchenSync/main $(filter %.c,$^) -o $@ -lm

test_beat_stamper: test_beat_stamper.c $(BEATSTAMP) $(UNITY) $(HEADERS)
	$(CC) $(CFLAGS) -I../LinkAudioPoC/main $(filter %.c,$^) -o $@ -lm
