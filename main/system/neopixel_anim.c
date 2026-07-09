/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include "neopixel_anim.h"

/* Timing in frames, assuming ~30 fps (see NEO_FRAME_MS in neopixel.c). */
#define NEO_ERR_PERIOD    6    /* ~5 Hz blink                       */
#define NEO_ERR_ON        3    /* bright frames per blink period    */
#define NEO_PAIR_PERIOD   20   /* ~1.5 Hz pulse                     */
#define NEO_BOOT_PERIOD   60   /* ~0.5 Hz breathe                   */
#define NEO_BOOT_FLOOR    64   /* breathe floor (never fully black) */
#define NEO_CONN_CYCLE    75   /* ~2.5 s connected cycle            */
#define NEO_BEAT_SLOT     6    /* frames per heartbeat beat slot    */
#define NEO_BEAT_ON       3    /* bright frames within a beat slot  */
#define NEO_BEAT_MAX      8    /* cap on beats (pads) shown         */
#define NEO_CONN_BASE_MIN 64   /* connected base breathe floor      */
#define NEO_CONN_BASE_AMP 40   /* connected base breathe amplitude  */

static const struct neo_rgb HUE_RED   = {255,   0,   0};
static const struct neo_rgb HUE_BLUE  = {  0,   0, 255};
static const struct neo_rgb HUE_AMBER = {255, 120,   0};
static const struct neo_rgb HUE_GREEN = {  0, 255,   0};

/* Triangle wave 0..255..0 across `period` frames. */
static uint8_t tri(uint32_t x, uint32_t period) {
    uint32_t half = period / 2;
    if (half == 0) {
        return 0;
    }
    x %= period;
    if (x < half) {
        return (uint8_t)(x * 255 / half);
    }
    return (uint8_t)(255 - (x - half) * 255 / half);
}

/* Scale a full-brightness hue by envelope `lvl` (0..255) and the `max_bright` cap. */
static struct neo_rgb apply(struct neo_rgb hue, uint8_t lvl, uint8_t max_bright) {
    struct neo_rgb out;
    out.r = (uint8_t)((uint32_t)hue.r * lvl * max_bright / (255u * 255u));
    out.g = (uint8_t)((uint32_t)hue.g * lvl * max_bright / (255u * 255u));
    out.b = (uint8_t)((uint32_t)hue.b * lvl * max_bright / (255u * 255u));
    return out;
}

enum neo_state neo_resolve_state(bool error, uint32_t inquiry_active, uint32_t conn_cnt) {
    if (error) {
        return NEO_STATE_ERROR;
    }
    if (inquiry_active) {
        return NEO_STATE_PAIRING;
    }
    if (conn_cnt > 0) {
        return NEO_STATE_CONNECTED;
    }
    return NEO_STATE_BOOTING;
}

static uint8_t conn_level(uint32_t conn_cnt, uint32_t frame) {
    uint32_t cycle = frame % NEO_CONN_CYCLE;
    uint32_t beats = (conn_cnt > NEO_BEAT_MAX) ? NEO_BEAT_MAX : conn_cnt;

    for (uint32_t i = 0; i < beats; i++) {
        uint32_t slot_start = i * NEO_BEAT_SLOT;
        if (cycle >= slot_start && cycle < slot_start + NEO_BEAT_ON) {
            return 255; /* heartbeat beat -> full bright */
        }
    }
    /* base gentle breathe, kept well below the beat level */
    return (uint8_t)(NEO_CONN_BASE_MIN + (uint32_t)tri(cycle, NEO_CONN_CYCLE) * NEO_CONN_BASE_AMP / 255);
}

struct neo_rgb neo_render(enum neo_state state, uint32_t conn_cnt, uint32_t frame, uint8_t max_bright) {
    switch (state) {
    case NEO_STATE_ERROR:
        return apply(HUE_RED, ((frame % NEO_ERR_PERIOD) < NEO_ERR_ON) ? 255 : 0, max_bright);
    case NEO_STATE_PAIRING:
        return apply(HUE_BLUE, tri(frame, NEO_PAIR_PERIOD), max_bright);
    case NEO_STATE_CONNECTED:
        return apply(HUE_GREEN, conn_level(conn_cnt, frame), max_bright);
    case NEO_STATE_BOOTING:
    default: {
        uint8_t lvl = (uint8_t)(NEO_BOOT_FLOOR +
            (uint32_t)tri(frame, NEO_BOOT_PERIOD) * (255 - NEO_BOOT_FLOOR) / 255);
        return apply(HUE_AMBER, lvl, max_bright);
    }
    }
}
