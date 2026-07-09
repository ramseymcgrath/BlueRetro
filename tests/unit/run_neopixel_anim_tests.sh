#!/usr/bin/env bash
# Host unit tests for the pure NeoPixel animation/state logic.
# neopixel_anim.c depends only on stdint/stdbool — no ESP-IDF required.
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root
cc -std=c11 -Wall -Wextra -Werror \
   -Imain/system \
   tests/unit/test_neopixel_anim.c \
   main/system/neopixel_anim.c \
   -o /tmp/neopixel_anim_tests
/tmp/neopixel_anim_tests
