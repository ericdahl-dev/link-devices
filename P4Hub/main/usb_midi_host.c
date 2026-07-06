/*
 * P4Hub glue: USB-MIDI host (P4-003/005). Enumerates a USB-MIDI device on the
 * Type-A OTG port, claims its MIDIStreaming interface (Audio class / subclass
 * 0x03), and sends 4-byte USB-MIDI event packets out the bulk OUT endpoint.
 * Proven in the scratchpad p4_midi_clock spike. Packet *encoding* lives in the
 * pure usb_midi_pack.c; this file is the USB I/O.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "usb_midi_host.h"
#include "midi_clock_in.h"   /* feed incoming 0xF8 timing to the BPM tracker (P4-011) */

static const char *TAG = "usb_midi_host";

static usb_host_client_handle_t s_client   = NULL;
static usb_device_handle_t      s_dev      = NULL;
static volatile uint8_t         s_new_addr = 0;
static volatile bool            s_new_dev  = false;
static volatile bool            s_gone     = false;
static volatile bool            s_ready    = false;

static uint8_t s_iface = 0, s_ep_out = 0, s_ep_in = 0;
static uint16_t s_in_mps = 64;

static usb_transfer_t *s_out = NULL, *s_in = NULL;
static volatile bool   s_out_busy = false;
static volatile uint32_t s_tx = 0, s_out_err = 0, s_dropped = 0;
static volatile uint32_t s_rx_clocks = 0;   /* 0xF8 seen on IN (loopback proof) */

/* ---- transfer callbacks (client-event context) --------------------------- */
static void out_cb(usb_transfer_t *t)
{
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) s_tx++; else s_out_err++;
    s_out_busy = false;
}
static void in_cb(usb_transfer_t *t)
{
    if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
        int64_t now = esp_timer_get_time();
        for (int i = 0; i + 1 < t->actual_num_bytes; i += 4)
            if (t->data_buffer[i + 1] == 0xF8) {
                s_rx_clocks++;                 /* loopback proof counter */
                midi_clock_in_pulse(now);      /* -> BPM tracker (P4-011) */
            }
    }
    if (s_ready) { t->num_bytes = s_in_mps; usb_host_transfer_submit(t); }
}

static void client_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        s_new_addr = msg->new_dev.address; s_new_dev = true;
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        s_gone = true;
    }
}

/* Find the MIDIStreaming interface (Audio class 0x01 / subclass 0x03) + its bulk
 * OUT and IN endpoints in the config descriptor. */
static bool find_midi_endpoints(const usb_config_desc_t *cfg)
{
    const uint8_t *p = (const uint8_t *)cfg, *end = p + cfg->wTotalLength;
    bool in_midi = false, got_out = false, got_in = false;
    while (p + 2 <= end && p[0] >= 2) {
        uint8_t bLen = p[0], bType = p[1];
        if (bType == 0x04 && bLen >= 9) {
            in_midi = (p[5] == 0x01 && p[6] == 0x03);
            if (in_midi) s_iface = p[2];
        } else if (in_midi && bType == 0x05 && bLen >= 7) {
            uint8_t addr = p[2], attr = p[3];
            uint16_t mps = p[4] | (p[5] << 8);
            if ((attr & 0x03) == 0x02) {
                if (addr & 0x80) { s_ep_in = addr; s_in_mps = mps ? mps : 64; got_in = true; }
                else             { s_ep_out = addr; got_out = true; }
            }
        }
        p += bLen;
    }
    return got_out && got_in;
}

static void setup_device(void)
{
    ESP_ERROR_CHECK(usb_host_device_open(s_client, s_new_addr, &s_dev));
    const usb_config_desc_t *cfg;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(s_dev, &cfg));
    if (!find_midi_endpoints(cfg)) {
        ESP_LOGE(TAG, "no MIDIStreaming endpoints");
        usb_host_device_close(s_client, s_dev); s_dev = NULL; return;
    }
    ESP_ERROR_CHECK(usb_host_interface_claim(s_client, s_dev, s_iface, 0));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(64, 0, &s_out));
    ESP_ERROR_CHECK(usb_host_transfer_alloc(64, 0, &s_in));
    s_out->device_handle = s_dev; s_out->bEndpointAddress = s_ep_out; s_out->callback = out_cb;
    s_in->device_handle  = s_dev; s_in->bEndpointAddress  = s_ep_in;  s_in->callback  = in_cb;
    s_ready = true;
    s_in->num_bytes = s_in_mps;
    usb_host_transfer_submit(s_in);
    ESP_LOGI(TAG, "USB-MIDI ready: iface %u OUT 0x%02x IN 0x%02x", s_iface, s_ep_out, s_ep_in);
}

static void teardown_device(void)
{
    s_ready = false;
    if (s_out) { usb_host_transfer_free(s_out); s_out = NULL; }
    if (s_in)  { usb_host_transfer_free(s_in);  s_in  = NULL; }
    if (s_dev) {
        usb_host_interface_release(s_client, s_dev, s_iface);
        usb_host_device_close(s_client, s_dev);
        s_dev = NULL;
    }
    ESP_LOGW(TAG, "USB-MIDI device gone");
}

static void usb_lib_task(void *arg)
{
    while (1) { uint32_t f; usb_host_lib_handle_events(portMAX_DELAY, &f); }
}

static void client_task(void *arg)
{
    const usb_host_client_config_t cc = {
        .is_synchronous = false, .max_num_event_msg = 5,
        .async = { .client_event_callback = client_cb, .callback_arg = NULL },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cc, &s_client));
    while (1) {
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(50));
        if (s_new_dev) { s_new_dev = false; setup_device(); }
        if (s_gone)    { s_gone = false;    teardown_device(); }
    }
}

/* ---- public API ---------------------------------------------------------- */
void usb_midi_host_start(void)
{
    const usb_host_config_t hc = { .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    ESP_ERROR_CHECK(usb_host_install(&hc));
    xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 5, NULL);
    xTaskCreate(client_task,  "usb_cli", 4096, NULL, 5, NULL);
}

bool usb_midi_host_ready(void) { return s_ready; }

uint32_t usb_midi_host_rx_clocks(void) { return s_rx_clocks; }

uint32_t usb_midi_host_tx(void) { return s_tx; }

void usb_midi_host_send(const uint8_t* data, int len)
{
    if (!s_ready || len <= 0 || len > 64) return;
    if (s_out_busy) { s_dropped++; return; }   /* previous packet still in flight */
    memcpy(s_out->data_buffer, data, len);
    s_out->num_bytes = len;
    s_out_busy = true;
    if (usb_host_transfer_submit(s_out) != ESP_OK) { s_out_busy = false; s_out_err++; }
}
