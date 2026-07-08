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
MIDICALC = ../X32MidiClock/midi_bpm_calc.c
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
USBMIDIPACK = ../X32Link/usb_midi_pack.c
BEATCLOCK = ../X32Link/beat_clock.c
BEATSOURCE = ../X32Link/beat_source.c
P4STATUS = ../KitchenSync/main/ks_status.c
P4CONFIG = ../KitchenSync/main/ks_config.c
P4FORM  = ../KitchenSync/main/ks_form.c
MIDICLOCKIN = ../KitchenSync/main/midi_clock_in.c
WIFICONNPOLICY = ../X32Link/wifi_conn_policy.c
TRANSPORT = ../X32Link/transport.c
CLOCKOUTPUT = ../X32Link/clock_output.c
MVOICE = ../X32Link/metronome_voice.c
SWING   = ../X32Link/swing.c
METROSTRIP = ../X32Link/metro_strip.c

.PHONY: all clean

all: test_bpm_tracker test_link_protocol test_link_measurement test_link_phase test_led_phase test_wifi_down_blink test_app_config test_osc_out test_midi_bpm_calc test_bpm_publisher test_osc_in test_web_status_json test_touch_ui test_tempo_snapshot test_bar test_midi_clock_out test_clock_ticker test_beat_synth test_link_measurement_session test_session_timeline test_usb_midi_pack test_beat_clock test_ks_status test_ks_config test_ks_form test_metronome test_transport test_clock_output test_metronome_voice test_beat_source test_swing test_midi_clock_in test_metro_strip test_wifi_conn_policy
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
	./check_docs.sh

test_bpm_tracker: test_bpm_tracker.c $(TRACKER) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_link_protocol: test_link_protocol.c $(PROTO) $(SESSIONTL) $(PHASE) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_link_measurement: test_link_measurement.c $(MEAS) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_link_phase: test_link_phase.c $(PHASE) $(PROTO) $(SESSIONTL) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_led_phase: test_led_phase.c $(LEDPHASE) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_wifi_down_blink: test_wifi_down_blink.c $(WIFIBLINK) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_app_config: test_app_config.c $(CFG) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_osc_out: test_osc_out.c $(OSC) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_midi_bpm_calc: test_midi_bpm_calc.c $(MIDICALC) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_bpm_publisher: test_bpm_publisher.c $(PUB) $(BAR) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_osc_in: test_osc_in.c $(OSCIN) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_web_status_json: test_web_status_json.c $(STATUSJSON) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_axs5106l: test_axs5106l.c $(AXS) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_touch_ui: test_touch_ui.c $(TOUCHUI) $(CFG) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_tempo_snapshot: test_tempo_snapshot.c $(SNAP) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_bar: test_bar.c $(BAR) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_midi_clock_out: test_midi_clock_out.c $(MIDIOUT) $(CLOCKTICK) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_clock_ticker: test_clock_ticker.c $(CLOCKTICK) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_beat_synth: test_beat_synth.c $(BEATSYNTH) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_link_measurement_session: test_link_measurement_session.c $(SESSION) $(PHASE) $(PROTO) $(SESSIONTL) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_session_timeline: test_session_timeline.c $(SESSIONTL) $(PHASE) $(PROTO) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_usb_midi_pack: test_usb_midi_pack.c $(USBMIDIPACK) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_beat_clock: test_beat_clock.c $(BEATCLOCK) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_beat_source: test_beat_source.c $(BEATSOURCE) $(BEATCLOCK) $(PHASE) $(PROTO) $(MEAS) $(SESSIONTL) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_ks_status: test_ks_status.c $(P4STATUS) $(UNITY)
	$(CC) $(CFLAGS) -I../KitchenSync/main $^ -o $@

clean:
	rm -f test_bpm_tracker test_link_protocol test_link_measurement test_link_phase test_led_phase test_wifi_down_blink test_app_config test_osc_out test_midi_bpm_calc test_bpm_publisher test_osc_in test_web_status_json test_axs5106l test_touch_ui test_tempo_snapshot test_bar

test_ks_config: test_ks_config.c $(P4CONFIG) $(UNITY)
	$(CC) $(CFLAGS) -I../KitchenSync/main $^ -o $@

test_ks_form: test_ks_form.c $(P4FORM) $(P4CONFIG) $(UNITY)
	$(CC) $(CFLAGS) -I../KitchenSync/main $^ -o $@

test_midi_clock_in: test_midi_clock_in.c $(MIDICLOCKIN) $(MIDICALC) $(UNITY)
	$(CC) $(CFLAGS) -I../KitchenSync/main $^ -o $@ -lm

test_metronome: test_metronome.c $(METRO) $(CLOCKTICK) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_transport: test_transport.c $(TRANSPORT) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_clock_output: test_clock_output.c $(CLOCKOUTPUT) $(CLOCKTICK) $(SWING) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_metronome_voice: test_metronome_voice.c $(MVOICE) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_swing: test_swing.c $(SWING) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_metro_strip: test_metro_strip.c $(METROSTRIP) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@ -lm

test_wifi_conn_policy: test_wifi_conn_policy.c $(WIFICONNPOLICY) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@
