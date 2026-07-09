/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side unit tests for the pure NeoPixel state/animation logic.
 * Build & run: tests/unit/run_neopixel_anim_tests.sh
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "neopixel_anim.h"

#define MAXB 64

static void test_resolve_priority(void) {
    assert(neo_resolve_state(true, 1, 4) == NEO_STATE_ERROR);   /* error wins */
    assert(neo_resolve_state(true, 0, 0) == NEO_STATE_ERROR);
    assert(neo_resolve_state(false, 1, 4) == NEO_STATE_PAIRING); /* pairing > connected */
    assert(neo_resolve_state(false, 1, 0) == NEO_STATE_PAIRING);
    assert(neo_resolve_state(false, 0, 2) == NEO_STATE_CONNECTED);
    assert(neo_resolve_state(false, 0, 0) == NEO_STATE_BOOTING); /* fallback */
    printf("ok  test_resolve_priority\n");
}

static void test_error_blinks(void) {
    struct neo_rgb on  = neo_render(NEO_STATE_ERROR, 0, 0, MAXB); /* frame 0 -> on  */
    struct neo_rgb off = neo_render(NEO_STATE_ERROR, 0, 3, MAXB); /* frame 3 -> off */
    assert(on.r == MAXB && on.g == 0 && on.b == 0);
    assert(off.r == 0 && off.g == 0 && off.b == 0);
    printf("ok  test_error_blinks\n");
}

static void test_pairing_is_blue_and_animated(void) {
    struct neo_rgb a = neo_render(NEO_STATE_PAIRING, 0, 0, MAXB);
    struct neo_rgb b = neo_render(NEO_STATE_PAIRING, 0, 5, MAXB);
    assert(a.r == 0 && a.g == 0);
    assert(b.r == 0 && b.g == 0);
    assert(a.b != b.b); /* animated, not static */
    printf("ok  test_pairing_is_blue_and_animated\n");
}

static void test_booting_is_amber_and_never_black(void) {
    for (uint32_t f = 0; f < 60; f++) {
        struct neo_rgb c = neo_render(NEO_STATE_BOOTING, 0, f, MAXB);
        assert(c.b == 0);                    /* amber: no blue        */
        assert(c.r >= c.g);                  /* amber: red-dominant   */
        assert(c.r > 0);                     /* breathe floor lit     */
        assert(c.r <= MAXB && c.g <= MAXB);  /* cap respected         */
    }
    printf("ok  test_booting_is_amber_and_never_black\n");
}

static uint32_t count_beats(uint32_t conn_cnt) {
    uint32_t count = 0;
    bool prev_bright = false;
    for (uint32_t f = 0; f < 75; f++) { /* one connected cycle */
        struct neo_rgb c = neo_render(NEO_STATE_CONNECTED, conn_cnt, f, MAXB);
        bool bright = c.g >= (uint8_t)(MAXB * 7 / 10);
        if (bright && !prev_bright) {
            count++;
        }
        prev_bright = bright;
    }
    return count;
}

static void test_connected_is_green_and_counts_beats(void) {
    struct neo_rgb c = neo_render(NEO_STATE_CONNECTED, 1, 30, MAXB);
    assert(c.g >= c.r && c.g >= c.b); /* green-dominant */
    assert(count_beats(1) == 1);
    assert(count_beats(2) == 2);
    assert(count_beats(3) == 3);
    assert(count_beats(8) == 8);
    assert(count_beats(10) == 8); /* capped */
    printf("ok  test_connected_is_green_and_counts_beats\n");
}

static void test_brightness_cap(void) {
    enum neo_state states[] = {NEO_STATE_ERROR, NEO_STATE_PAIRING, NEO_STATE_BOOTING, NEO_STATE_CONNECTED};
    for (int s = 0; s < 4; s++) {
        for (uint32_t f = 0; f < 150; f++) {
            struct neo_rgb c = neo_render(states[s], 4, f, MAXB);
            assert(c.r <= MAXB && c.g <= MAXB && c.b <= MAXB);
        }
    }
    printf("ok  test_brightness_cap\n");
}

int main(void) {
    test_resolve_priority();
    test_error_blinks();
    test_pairing_is_blue_and_animated();
    test_booting_is_amber_and_never_black();
    test_connected_is_green_and_counts_beats();
    test_brightness_cap();
    printf("\nAll NeoPixel animation tests passed.\n");
    return 0;
}
