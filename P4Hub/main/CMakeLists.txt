# P4Hub reuses the pure, host-tested engines from X32Link UNCHANGED (ADR-0003).
# The .c files stay single-source in ../X32Link and are compiled into this
# ESP-IDF app by relative path — no copies, no forks. Their Unity suites in
# ../test keep covering them.
set(X32LINK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../X32Link")

idf_component_register(
    SRCS "p4hub_main.c"
         "${X32LINK_DIR}/clock_ticker.c"
    INCLUDE_DIRS "." "${X32LINK_DIR}"
    PRIV_REQUIRES esp_timer)
