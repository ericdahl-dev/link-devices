set(CMAKE_CXX_STANDARD 17)

# i2s_audio_bus.c reused from KitchenSync by relative path (ADR-0003 style,
# single source): the P4-020-validated ES8311 + full-duplex I2S bring-up,
# including the mic gain and TX-as-clock-driver fixes.
set(KS_DIR ${CMAKE_CURRENT_LIST_DIR}/../../KitchenSync/main)

idf_component_register(SRCS main.cpp beat_stamper.c ${KS_DIR}/i2s_audio_bus.c
                       INCLUDE_DIRS . ${KS_DIR}
                       PRIV_REQUIRES driver esp_driver_i2s esp_timer esp_wifi esp_event esp_netif nvs_flash)

# The SDK's Link engine spins timers/sockets on FreeRTOS tasks; unpinned is the
# upstream example default.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_ESP_TASK_CORE_ID=tskNO_AFFINITY)

# asio (the SDK's IoContext backbone) throws; the SDK's esp32 example builds
# with exceptions on. CONFIG_COMPILER_CXX_EXCEPTIONS in sdkconfig.defaults is
# the project-wide half of this.
target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)

# Ableton::Link imported interface target -- include path + platform defines.
# ESP_PLATFORM (defined by IDF) makes platforms/Config.hpp pick the esp32 layer.
include(${CMAKE_CURRENT_LIST_DIR}/../third_party/link/AbletonLinkConfig.cmake)
target_link_libraries(${COMPONENT_LIB} PUBLIC Ableton::Link)
