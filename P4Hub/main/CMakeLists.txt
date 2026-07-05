# P4Hub reuses the pure, host-tested engines from X32Link UNCHANGED (ADR-0003),
# compiled in by relative path — single source, no copies/forks. Their Unity
# suites in ../test keep covering them.
set(X32LINK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../X32Link")

idf_component_register(
    SRCS "p4hub_main.c"
         "wifi_link.c"
         "usb_midi_host.c"
         "p4hub_web.cpp"
         "p4hub_status.c"
         "p4hub_config.c"
         "p4hub_config_nvs.c"
         "${X32LINK_DIR}/clock_ticker.c"
         "${X32LINK_DIR}/beat_clock.c"
         "${X32LINK_DIR}/usb_midi_pack.c"
         "${X32LINK_DIR}/link_protocol.c"
    INCLUDE_DIRS "." "${X32LINK_DIR}"
    PRIV_REQUIRES esp_wifi nvs_flash esp_timer lwip esp_netif esp_event usb esp_http_server)
