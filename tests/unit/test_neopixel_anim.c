/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "neopixel_anim.h"

#define MAXB 64

static void test_resolve_priority(void) {
    assert(neo_resolve_port(true,  true,  true,  true)  == NEO_STATE_ERROR);
    assert(neo_resolve_port(true,  false, false, false) == NEO_STATE_ERROR);
    assert(neo_resolve_port(false, true,  true,  true)  == NEO_STATE_PAIRING);
    assert(neo_resolve_port(false, false, true,  true)  == NEO_STATE_CONN_ANALOG);
    assert(neo_resolve_port(false, false, true,  false) == NEO_STATE_CONN_DIGITAL);
    assert(neo_resolve_port(false, false, false, true)  == NEO_STATE_IDLE);
    assert(neo_resolve_port(false, false, false, false) == NEO_STATE_IDLE);
    printf("ok  test_resolve_priority\n");
}

static void test_error_blinks(void) {
    struct neo_rgb on  = neo_render(NEO_STATE_ERROR, 0, MAXB);
    struct neo_rgb off = neo_render(NEO_STATE_ERROR, 3, MAXB);
    assert(on.r == MAXB && on.g == 0 && on.b == 0);
    assert(off.r == 0 && off.g == 0 && off.b == 0);
    printf("ok  test_error_blinks\n");
}

static void test_pairing_blue_animated(void) {
    struct neo_rgb a = neo_render(NEO_STATE_PAIRING, 0, MAXB);
    struct neo_rgb b = neo_render(NEO_STATE_PAIRING, 5, MAXB);
    assert(a.r == 0 && a.g == 0 && b.r == 0 && b.g == 0);
    assert(a.b != b.b);
    printf("ok  test_pairing_blue_animated\n");
}

static void test_analog_steady_brighter_than_digital(void) {
    struct neo_rgb a0 = neo_render(NEO_STATE_CONN_ANALOG, 0, MAXB);
    struct neo_rgb a1 = neo_render(NEO_STATE_CONN_ANALOG, 17, MAXB);
    assert(a0.r == 0 && a0.b == 0 && a0.g == MAXB); /* green, full */
    assert(a0.g == a1.g);                           /* steady across frames */

    struct neo_rgb d0 = neo_render(NEO_STATE_CONN_DIGITAL, 0, MAXB);
    uint8_t dmax = 0;
    bool varies = false;
    for (uint32_t f = 0; f < 50; f++) {
        struct neo_rgb d = neo_render(NEO_STATE_CONN_DIGITAL, f, MAXB);
        assert(d.r == 0 && d.b == 0);
        if (d.g > dmax) dmax = d.g;
        if (d.g != d0.g) varies = true;
    }
    assert(varies);          /* digital breathes */
    assert(dmax < a0.g);     /* digital dimmer than steady analog */
    printf("ok  test_analog_steady_brighter_than_digital\n");
}

static void test_idle_amber_never_black(void) {
    for (uint32_t f = 0; f < 60; f++) {
        struct neo_rgb c = neo_render(NEO_STATE_IDLE, f, MAXB);
        assert(c.b == 0);
        assert(c.r >= c.g);
        assert(c.r > 0);
        assert(c.r <= MAXB && c.g <= MAXB);
    }
    printf("ok  test_idle_amber_never_black\n");
}

static void test_brightness_cap(void) {
    enum neo_state st[] = {NEO_STATE_IDLE, NEO_STATE_CONN_DIGITAL, NEO_STATE_CONN_ANALOG,
                           NEO_STATE_PAIRING, NEO_STATE_ERROR};
    for (int s = 0; s < 5; s++) {
        for (uint32_t f = 0; f < 150; f++) {
            struct neo_rgb c = neo_render(st[s], f, MAXB);
            assert(c.r <= MAXB && c.g <= MAXB && c.b <= MAXB);
        }
    }
    printf("ok  test_brightness_cap\n");
}

int main(void) {
    test_resolve_priority();
    test_error_blinks();
    test_pairing_blue_animated();
    test_analog_steady_brighter_than_digital();
    test_idle_amber_never_black();
    test_brightness_cap();
    printf("\nAll NeoPixel animation tests passed.\n");
    return 0;
}
