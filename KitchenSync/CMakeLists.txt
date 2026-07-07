# KitchenSync — ESP32-P4 "hub tier" firmware (native ESP-IDF).
# See README.md and tasks/P4-002.md for why this target is ESP-IDF, not Arduino.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kitchensync)
