cmake_minimum_required(VERSION 3.10)
project(linkcli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Ableton Link SDK is vendored under LinkAudioPoC (with asio bundled at
# modules/asio-standalone). Override with -DLINK_SDK_DIR=... if it moves.
if(NOT DEFINED LINK_SDK_DIR)
  set(LINK_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../LinkAudioPoC/third_party/link")
endif()

if(NOT EXISTS "${LINK_SDK_DIR}/AbletonLinkConfig.cmake")
  message(FATAL_ERROR
    "Ableton Link SDK not found at ${LINK_SDK_DIR}\n"
    "Pass -DLINK_SDK_DIR=/path/to/link")
endif()

include("${LINK_SDK_DIR}/AbletonLinkConfig.cmake")

find_package(Threads REQUIRED)

add_executable(linkcli main.cpp)
target_link_libraries(linkcli Ableton::Link Threads::Threads)
