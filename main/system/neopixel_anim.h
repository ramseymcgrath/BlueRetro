/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NEOPIXEL_ANIM_H_
#define _NEOPIXEL_ANIM_H_

#include <stdint.h>
#include <stdbool.h>

/* Priority order (highest last): error > pairing > connected > idle. */
enum neo_state {
    NEO_STATE_IDLE = 0,      /* empty port / booting -> amber breathe */
    NEO_STATE_CONN_DIGITAL,  /* connected, digital   -> green breathe */
    NEO_STATE_CONN_ANALOG,   /* connected, analog    -> green steady  */
    NEO_STATE_PAIRING,       /* inquiry active       -> blue pulse    */
    NEO_STATE_ERROR,         /* latched error        -> red blink     */
};

struct neo_rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/* Pure: resolve one port's state. Global error/pairing override per-port. */
enum neo_state neo_resolve_port(bool error, bool pairing, bool connected, bool analog);

/* Pure: colour for `state` at animation `frame` (~30 fps), capped at `max_bright`. */
struct neo_rgb neo_render(enum neo_state state, uint32_t frame, uint8_t max_bright);

#endif /* _NEOPIXEL_ANIM_H_ */
