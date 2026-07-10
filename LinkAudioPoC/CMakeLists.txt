# P4-032 Link Audio PoC — Tier 1: official Ableton Link SDK as a live tempo
# peer on the ESP32-P4 (WiFi via the onboard C6 / ESP-Hosted, same recipe as
# KitchenSync). Standalone spike on purpose: the GPLv2 C++ SDK stays out of
# the KitchenSync production target until the PoC proves out (see
# docs/plans/2026-07-08-p4-link-audio-feasibility.md).
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(link_audio_poc)
