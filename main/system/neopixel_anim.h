/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _NEOPIXEL_ANIM_H_
#define _NEOPIXEL_ANIM_H_

#include <stdint.h>
#include <stdbool.h>

/* Priority order (highest last): error > pairing > connected > booting. */
enum neo_state {
    NEO_STATE_BOOTING = 0, /* powered, BT not ready / idle-empty -> amber breathe */
    NEO_STATE_CONNECTED,   /* >=1 pad connected                  -> green + beats  */
    NEO_STATE_PAIRING,     /* inquiry / discoverable active      -> blue pulse     */
    NEO_STATE_ERROR,       /* latched fatal error                -> red blink      */
};

struct neo_rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/* Pure: pick the highest-priority active state. */
enum neo_state neo_resolve_state(bool error, uint32_t inquiry_active, uint32_t conn_cnt);

/* Pure: colour for `state` at animation `frame` (~30 fps). `conn_cnt` sets the
 * connected heartbeat beat count. No channel exceeds `max_bright`. */
struct neo_rgb neo_render(enum neo_state state, uint32_t conn_cnt, uint32_t frame, uint8_t max_bright);

#endif /* _NEOPIXEL_ANIM_H_ */
