/*
 * P4-032 Tier 1: official Ableton Link SDK running on the ESP32-P4 as a live
 * tempo peer, WiFi via the onboard C6 (ESP-Hosted), verified against Ableton
 * Live. Success = Live's Link peer count includes this device and the logged
 * tempo tracks Live's.
 *
 * Deliberately minimal: inline STA connect (no AP fallback, no web UI, no
 * config) -- this spike answers "does the SDK's esp32 platform layer hold on
 * IDF v5.5 / P4 RISC-V", nothing else. Tier 2 swaps ableton::Link for
 * ableton::LinkAudio; Tier 3 feeds the mic. See
 * docs/plans/2026-07-08-p4-link-audio-feasibility.md.
 */
#include <ableton/LinkAudio.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include "beat_stamper.h"
#include "i2s_audio_bus.h"   // KitchenSync's P4-020-validated ES8311/I2S bring-up
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char* TAG = "link_poc";

// Keep the P4's 8KB TCM block OUT of the heap. Its capability set includes
// MALLOC_CAP_INTERNAL, and the best-fit allocator will happily place a small
// FreeRTOS task stack there (observed: an ESP-Hosted 5120B task stack) -- the
// first flash operation then dies on
//   assert: spi_flash_disable_interrupts_caches_and_other_cpu
//           (esp_task_stack_is_sane_cache_disabled)
// because task stacks in TCM aren't valid while the cache is off. Whether a
// stack lands there is allocation-order luck; this build lost the lottery
// (KitchenSync's image happens to win it). Reserving the region removes the
// dice entirely.
#include "heap_memory_layout.h"
// 0x30100044, not 0x30100000: the first 0x44 bytes are already a static
// reservation (boot log: "At 30100044 len 00001FBC (7 KiB): TCM"), and two
// reservations sharing a start address trip the sorted-regions assert in
// s_prepare_reserved_regions.
SOC_RESERVE_MEMORY_REGION(0x30100044, 0x30102000, poc_no_tcm_heap);

// The SDK's esp32 example stubs these too: lwIP has no if_nametoindex, and
// Link's IpV4/V6 interface scanner links against them.
extern "C" unsigned int if_nametoindex(const char* /*ifName*/)
{
  return 0;
}
extern "C" char* if_indextoname(unsigned int /*ifIndex*/, char* /*ifName*/)
{
  return nullptr;
}

// --- minimal STA connect (KitchenSync wifi_link.c, stripped of AP fallback) ---
static EventGroupHandle_t s_wifi_events;
static constexpr int kGotIp = BIT0;

static void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* data)
{
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
  {
    ESP_LOGW(TAG, "disconnected, retrying");
    esp_wifi_connect();
  }
  else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
  {
    auto* e = static_cast<ip_event_got_ip_t*>(data);
    ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&e->ip_info.ip));
    xEventGroupSetBits(s_wifi_events, kGotIp);
  }
}

static void wifi_connect_blocking()
{
  s_wifi_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, nullptr));

  wifi_config_t wc = {};
  strncpy(reinterpret_cast<char*>(wc.sta.ssid), CONFIG_POC_WIFI_SSID, sizeof(wc.sta.ssid) - 1);
  strncpy(reinterpret_cast<char*>(wc.sta.password), CONFIG_POC_WIFI_PASSWORD, sizeof(wc.sta.password) - 1);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_LOGI(TAG, "connecting to SSID:%s", CONFIG_POC_WIFI_SSID);
  ESP_ERROR_CHECK(esp_wifi_start());

  xEventGroupWaitBits(s_wifi_events, kGotIp, pdFALSE, pdTRUE, portMAX_DELAY);
}

// --- Tier 3: mic -> LinkAudio sink -------------------------------------------
// Tier 1 (base Link tempo peer) passed 2026-07-09: first build, tempo/beats
// tracked a live session over C6/ESP-Hosted WiFi. Tier 2 passed same night:
// "P4 Mic" channel visible in Live 12.4, Live's own channels discovered here
// (after patching the SDK's 8K asio task stack, see Context.hpp). Tier 3:
// stream the onboard mic into the sink with beat_stamper-continuous
// beatsAtBufferBegin; success = Live plays the mic audio in sync.
// 44.1kHz stereo requirement (2026-07-10). 512 frames = ~11.6ms blocks.
// "Stereo" here is FORMAT, not capture: the ES8311's ADC is mono and the
// codec mirrors it into both I2S slots (P4-020 finding), so both channels
// carry the same mic. True 2-channel capture needs different input hardware
// (Tier 4 USB audio interface, or an external stereo ADC).
// Default until the session's rate is sniffed (below). Rate mismatch is
// audible: streaming 44.1k into a 48k Live session put the receiver resampler
// in the path, and clock-skewed stamps fought it -- "film projector" flutter,
// a spectral comb at block rate (86Hz = 44100/512) in the recording analysis
// (2026-07-10). Matching the session rate removes that resampler entirely.
static constexpr uint32_t kDefaultRate = 48000;
static constexpr uint32_t kBlockFrames = 512;
static constexpr uint32_t kChannels    = 2;
static constexpr size_t kMaxSinkSamples = kBlockFrames * kChannels;

// Session-rate sniffing: Live announces channels but the channel list carries
// no rate -- only received BUFFERS do (LinkAudioSource::BufferHandle::Info).
// So subscribe to the first remote channel, read one buffer's sampleRate,
// unsubscribe, reclock. 0 = not yet sniffed.
static std::atomic<uint32_t> g_session_rate{0};
static constexpr double kQuantum = 4.0;

static void link_task(void*)
{
  auto link_ptr = std::make_unique<ableton::LinkAudio>(120.0, "KitchenSync P4");
  auto& link = *link_ptr;
  link.enable(true);
  link.enableLinkAudio(true);

  ableton::LinkAudioSink sink(link, "P4 Mic", kMaxSinkSamples);

  // Mic capture: shared-bus RX (stereo slots, ES8311's mono ADC duplicated in
  // both -- P4-020 hardware finding; left slot used). TX is already enabled by
  // audio_bus_init() as the shared clock driver.
  if (i2s_channel_enable(audio_bus_rx()) != ESP_OK)
  {
    printf("[link_poc] RX enable failed -- no mic\n");
    vTaskDelete(nullptr);
  }

  static int16_t stereo[kBlockFrames * kChannels];
  BeatStamper stamper;
  beat_stamper_reset(&stamper);

  uint32_t committed = 0, skipped = 0, block = 0;
  uint32_t rate = kDefaultRate;
  std::optional<ableton::LinkAudioSource> sniffer;

  while (true)
  {
    size_t bytes = 0;
    if (i2s_channel_read(audio_bus_rx(), stereo, sizeof(stereo), &bytes, portMAX_DELAY) != ESP_OK)
    {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    const uint32_t frames = bytes / sizeof(int16_t) / kChannels;

    // --- session-rate adaptation ------------------------------------------
    const uint32_t sniffed = g_session_rate.load(std::memory_order_relaxed);
    if (sniffed != 0 && sniffer)
    {
      printf("[link_poc] session rate sniffed: %u Hz\n", (unsigned)sniffed);
      sniffer.reset();   // job done; destroy OFF the Link thread
    }
    if (sniffed != 0 && sniffed != rate)
    {
      printf("[link_poc] session rate %u != ours %u -- reclocking\n",
             (unsigned)sniffed, (unsigned)rate);
      if (audio_bus_reclock(sniffed) && i2s_channel_enable(audio_bus_rx()) == ESP_OK)
      {
        rate = sniffed;
        beat_stamper_reset(&stamper);   // old anchor is in the old rate's time
      }
      else
      {
        printf("[link_poc] reclock failed, staying at %u\n", (unsigned)rate);
        g_session_rate.store(rate, std::memory_order_relaxed);  // stop retrying
      }
      continue;   // this block straddled the reclock; drop it
    }
    if (sniffed == 0 && !sniffer)
    {
      const auto chans = link.channels();
      if (!chans.empty())
      {
        printf("[link_poc] sniffing session rate from '%s'\n", chans[0].name.c_str());
        sniffer.emplace(link, chans[0].id,
          [](ableton::LinkAudioSource::BufferHandle bh)
          {
            uint32_t expect = 0;   // only the first buffer matters
            g_session_rate.compare_exchange_strong(expect, bh.info.sampleRate);
          });
      }
    }
    // -----------------------------------------------------------------------

    // Stamp at the block's END (the read just returned), then commit.
    const auto state = link.captureAudioSessionState();
    const double endBeats =
      state.beatAtTime(link.clock().micros(), kQuantum);
    const double bps = state.tempo() / 60.0;
    const double beginBeats =
      beat_stamper_stamp(&stamper, endBeats, bps, frames, rate);

    ableton::LinkAudioSink::BufferHandle h(sink);
    if (h && h.maxNumSamples >= frames * kChannels)
    {
      memcpy(h.samples, stereo, frames * kChannels * sizeof(int16_t));
      if (h.commit(state, beginBeats, kQuantum, frames, kChannels, rate))
        ++committed;
      else
        ++skipped;
    }
    else
    {
      ++skipped;   // no source subscribed / no buffer -- normal when idle
    }

    if (++block % 64 == 0)   // ~1s at 16ms blocks
    {
      printf("[link_poc] peers:%zu tempo:%.2f committed:%lu skipped:%lu channels:%zu\n",
             link.numPeers(), state.tempo(), (unsigned long)committed,
             (unsigned long)skipped, link.channels().size());
    }
  }
}

extern "C" void app_main()
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_connect_blocking();

  // ES8311 + full-duplex I2S (P4-020's shared bus: TX enabled as clock driver,
  // mic PGA 24dB, analog mic mode). Must precede the capture loop's RX enable.
  audio_bus_init(kDefaultRate);
  if (!audio_bus_ready())
  {
    printf("[link_poc] audio bus failed -- Tier 3 needs the mic\n");
    return;
  }

  // 16K was always enough HERE -- the Tier 2 stack-protection faults were the
  // SDK's own asio service task (8K, hardcoded, patched to 16K in the vendored
  // Context.hpp), which shares the name "link" and is where LinkAudio's
  // processors actually run. Named "poc" to keep the two apart in panics.
  xTaskCreate(link_task, "poc", 16384, nullptr, 5, nullptr);
}
