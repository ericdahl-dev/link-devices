// Hardware MIDI OUT over UART1 + downbeat strobe — see midi_uart_out.h (ESP-015).
#include "midi_uart_out.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "midi_uart_out";

// UART0 is the console; 1..4 are free. UART1 carries this TX now and can add RX
// (MIDI IN, ESP-016) on the same controller later.
#define MIDI_UART        UART_NUM_1
#define MIDI_BAUD        31250
#define MIDI_TX_RING     256   // small: a mirror that falls seconds behind is useless

static bool s_ready;
static int  s_strobe = -1;

void midi_uart_out_start(int tx_gpio, int strobe_gpio)
{
    const uart_config_t cfg = {
        .baud_rate  = MIDI_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,   // 31250 divides cleanly -> no baud error
    };
    esp_err_t e = uart_driver_install(MIDI_UART, MIDI_TX_RING, MIDI_TX_RING, 0, NULL, 0);
    if (e == ESP_OK) e = uart_param_config(MIDI_UART, &cfg);
    if (e == ESP_OK) e = uart_set_pin(MIDI_UART, tx_gpio, UART_PIN_NO_CHANGE,
                                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) { ESP_LOGE(TAG, "UART init failed: %s", esp_err_to_name(e)); return; }

    if (strobe_gpio >= 0) {
        gpio_config_t gc = {
            .pin_bit_mask = 1ULL << strobe_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&gc);
        gpio_set_level(strobe_gpio, 0);
        s_strobe = strobe_gpio;
    }
    s_ready = true;
    ESP_LOGI(TAG, "MIDI OUT on GPIO%d @ %d baud, downbeat strobe GPIO%d",
             tx_gpio, MIDI_BAUD, strobe_gpio);
}

void midi_uart_out_byte(uint8_t status)
{
    if (!s_ready) return;
    // One byte into the driver's TX ring; the peripheral shifts it out under
    // interrupt. Never uart_wait_tx_done() here -- that would burn 320us of the
    // 1 ms clock budget waiting for the shift register to drain.
    uart_write_bytes(MIDI_UART, &status, 1);
}

void midi_uart_out_strobe(bool level)
{
    if (s_strobe >= 0) gpio_set_level(s_strobe, level ? 1 : 0);
}
