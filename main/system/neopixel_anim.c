/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */
#include "neopixel_anim.h"

/* Timing in frames, ~30 fps. */
#define NEO_ERR_PERIOD   6
#define NEO_ERR_ON       3
#define NEO_PAIR_PERIOD  20
#define NEO_IDLE_PERIOD  60
#define NEO_IDLE_FLOOR   40
#define NEO_DIG_PERIOD   50
#define NEO_DIG_FLOOR    64
#define NEO_DIG_PEAK     160

static const struct neo_rgb HUE_RED   = {255,   0,   0};
static const struct neo_rgb HUE_BLUE  = {  0,   0, 255};
static const struct neo_rgb HUE_AMBER = {255, 120,   0};
static const struct neo_rgb HUE_GREEN = {  0, 255,   0};

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

static struct neo_rgb apply(struct neo_rgb hue, uint8_t lvl, uint8_t max_bright) {
    struct neo_rgb out;
    out.r = (uint8_t)((uint32_t)hue.r * lvl * max_bright / (255u * 255u));
    out.g = (uint8_t)((uint32_t)hue.g * lvl * max_bright / (255u * 255u));
    out.b = (uint8_t)((uint32_t)hue.b * lvl * max_bright / (255u * 255u));
    return out;
}

enum neo_state neo_resolve_port(bool error, bool pairing, bool connected, bool analog) {
    if (error) {
        return NEO_STATE_ERROR;
    }
    if (pairing) {
        return NEO_STATE_PAIRING;
    }
    if (connected) {
        return analog ? NEO_STATE_CONN_ANALOG : NEO_STATE_CONN_DIGITAL;
    }
    return NEO_STATE_IDLE;
}

struct neo_rgb neo_render(enum neo_state state, uint32_t frame, uint8_t max_bright) {
    switch (state) {
    case NEO_STATE_ERROR:
        return apply(HUE_RED, ((frame % NEO_ERR_PERIOD) < NEO_ERR_ON) ? 255 : 0, max_bright);
    case NEO_STATE_PAIRING:
        return apply(HUE_BLUE, tri(frame, NEO_PAIR_PERIOD), max_bright);
    case NEO_STATE_CONN_ANALOG:
        return apply(HUE_GREEN, 255, max_bright); /* steady bright */
    case NEO_STATE_CONN_DIGITAL:
        return apply(HUE_GREEN,
            (uint8_t)(NEO_DIG_FLOOR + (uint32_t)tri(frame, NEO_DIG_PERIOD) * (NEO_DIG_PEAK - NEO_DIG_FLOOR) / 255),
            max_bright);
    case NEO_STATE_IDLE:
    default:
        return apply(HUE_AMBER,
            (uint8_t)(NEO_IDLE_FLOOR + (uint32_t)tri(frame, NEO_IDLE_PERIOD) * (255 - NEO_IDLE_FLOOR) / 255),
            max_bright);
    }
}
