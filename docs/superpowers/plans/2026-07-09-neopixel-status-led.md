# NeoPixel Status Indicator Implementation Plan

> **⚠️ SUPERSEDED** — this is the original *single-pixel* plan (one WS2812 on GPIO4,
> analog-mode LEDs left intact). The shipped design is the **per-port** revision:
> two pixels on GPIO12/15 that retire the discrete analog LEDs. Follow
> [`2026-07-09-neopixel-per-port-revision.md`](2026-07-09-neopixel-per-port-revision.md)
> and the addendum in the design spec instead. Kept for history.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a single WS2812 ("NeoPixel") as the primary external status light for a PS2-only BlueRetro build, without touching the existing on-board LEDs.

**Architecture:** A self-contained `main/system/neopixel` module. All decision/animation logic lives in an ESP-IDF-free pure unit (`neopixel_anim.[ch]`) that is host-unit-tested; a thin glue file (`neopixel.c`) drives the pixel via Espressif's `led_strip` component from a low-priority task pinned to core 0, polling BlueRetro's existing BT status getters. `err_led` and the analog-mode LEDs are untouched.

**Tech Stack:** C, ESP-IDF v5.5.0, FreeRTOS, `espressif/led_strip` v3.0.3 (RMT backend), host `cc` for unit tests.

## Global Constraints

- Target: ESP32, PS2 build only — gate on `CONFIG_BLUERETRO_SYSTEM_PSX_PS2` (config file `configs/hw2/playstation`).
- ESP-IDF **v5.5.0**; `led_strip` dependency `"^3.0.3"` (requires idf >= 5.0).
- Leave `err_led` (LEDC, GPIO17) and analog-mode LEDs (GPIO12/15) behaviour **bit-for-bit unchanged**.
- Reuse existing signals only — pairing: `bt_hci_get_inquiry()`; connected count: `bt_host_get_flag_dev_cnt(BT_DEV_HID_INIT_DONE)`; error: new 1-line `err_led_get()`.
- No hand-rolled WS2812 driver — WS2812 timing/encoding is `led_strip`.
- Render task: `xTaskCreatePinnedToCore(..., core 0)`, priority 5.
- Defaults: data pin **GPIO4**, max brightness **64/255**, feature **off by default** (`CONFIG_BLUERETRO_NEOPIXEL`).
- Do not modify the core-1 PS2 SPI path (`ps_spi.c`).
- Branch: `neopixel-status-led`. Commit after every task.
- License header on new C files: `Copyright (c) 2026, Jacques Gagnon` / `SPDX-License-Identifier: Apache-2.0` (match repo convention).

## File Structure

- **Create** `main/system/neopixel_anim.h` — pure types (`enum neo_state`, `struct neo_rgb`) + `neo_resolve_state()`, `neo_render()`. No ESP-IDF includes.
- **Create** `main/system/neopixel_anim.c` — pure implementation.
- **Create** `main/system/neopixel.h` — `void neopixel_init(void);`
- **Create** `main/system/neopixel.c` — `led_strip` setup + render task + getter polling.
- **Create** `main/idf_component.yml` — declares the `led_strip` dependency for the `main` component.
- **Create** `tests/unit/test_neopixel_anim.c` — host assert-based tests for the pure logic.
- **Create** `tests/unit/run_neopixel_anim_tests.sh` — compile + run the host tests.
- **Modify** `main/system/led.h` — declare `err_led_get()`.
- **Modify** `main/system/led.c` — implement `err_led_get()`.
- **Modify** `main/Kconfig.projbuild` — add 3 config options.
- **Modify** `main/CMakeLists.txt` — conditionally append the two new sources.
- **Modify** `main/main.c` — call `neopixel_init()` behind `#ifdef`.

---

### Task 1: Pure state/animation module (host TDD)

The only unit-testable surface. Pure C, no ESP-IDF — compiled and run on the host.

**Files:**
- Create: `main/system/neopixel_anim.h`
- Create: `main/system/neopixel_anim.c`
- Test: `tests/unit/test_neopixel_anim.c`
- Test harness: `tests/unit/run_neopixel_anim_tests.sh`

**Interfaces:**
- Produces:
  - `enum neo_state { NEO_STATE_BOOTING, NEO_STATE_CONNECTED, NEO_STATE_PAIRING, NEO_STATE_ERROR };`
  - `struct neo_rgb { uint8_t r, g, b; };`
  - `enum neo_state neo_resolve_state(bool error, uint32_t inquiry_active, uint32_t conn_cnt);`
  - `struct neo_rgb neo_render(enum neo_state state, uint32_t conn_cnt, uint32_t frame, uint8_t max_bright);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_neopixel_anim.c`:

```c
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
```

- [ ] **Step 2: Add the build/run harness**

Create `tests/unit/run_neopixel_anim_tests.sh`:

```bash
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
```

Then make it executable:

Run: `chmod +x tests/unit/run_neopixel_anim_tests.sh`

- [ ] **Step 3: Run the tests to verify they fail**

Run: `tests/unit/run_neopixel_anim_tests.sh`
Expected: FAIL — compile error, `fatal error: neopixel_anim.h: No such file or directory`.

- [ ] **Step 4: Create the pure header**

Create `main/system/neopixel_anim.h`:

```c
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
```

- [ ] **Step 5: Create the pure implementation**

Create `main/system/neopixel_anim.c`:

```c
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
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `tests/unit/run_neopixel_anim_tests.sh`
Expected: PASS — six `ok  ...` lines then `All NeoPixel animation tests passed.`

- [ ] **Step 7: Commit**

```bash
git add main/system/neopixel_anim.h main/system/neopixel_anim.c \
        tests/unit/test_neopixel_anim.c tests/unit/run_neopixel_anim_tests.sh
git commit -m "feat(neopixel): pure state/animation logic with host unit tests"
```

---

### Task 2: `err_led_get()` read-only accessor

Expose the existing error latch without changing any `err_led` behaviour.

**Files:**
- Modify: `main/system/led.h`
- Modify: `main/system/led.c`

**Interfaces:**
- Produces: `uint32_t err_led_get(void);` — returns non-zero when the latched error state is set.

- [ ] **Step 1: Declare the accessor**

In `main/system/led.h`, add after `void err_led_pulse(void);` (line 15):

```c
uint32_t err_led_get(void);
```

- [ ] **Step 2: Implement the accessor**

In `main/system/led.c`, add after `err_led_pulse()` (after line 98), before `err_led_get_pin()`:

```c
uint32_t err_led_get(void) {
    return atomic_test_bit(&led_flags, ERR_LED_SET);
}
```

(`led_flags`, `ERR_LED_SET`, and `atomic_test_bit` are already defined/included in this file — no other change. `err_led` behaviour is unchanged.)

- [ ] **Step 3: Commit**

```bash
git add main/system/led.h main/system/led.c
git commit -m "feat(led): add read-only err_led_get() accessor"
```

Verification of compilation happens in Task 4's build (this accessor has no host test; it is a trivial atomic read).

---

### Task 3: Kconfig options

**Files:**
- Modify: `main/Kconfig.projbuild`

- [ ] **Step 1: Add the three options**

In `main/Kconfig.projbuild`, insert this block immediately **before** the line `    choice` that is followed by `prompt "Select universal or specific system"` (i.e. right after the `BLUERETRO_HW2` config's `help` block):

```
    config BLUERETRO_NEOPIXEL
        bool "Enable NeoPixel (WS2812) status LED"
        depends on BLUERETRO_SYSTEM_PSX_PS2
        default n
        help
            Drive a single WS2812 "NeoPixel" as an external status indicator.
            PS2-only: other cores contend for the RMT peripheral and GPIOs.
            The on-board error LED is unaffected.

    config BLUERETRO_NEOPIXEL_PIN
        int "NeoPixel data GPIO"
        depends on BLUERETRO_NEOPIXEL
        default 4
        help
            Output-capable GPIO that is free on a PS2 build (4, 13, 16, 18 or 23).

    config BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS
        int "NeoPixel max brightness (0-255)"
        depends on BLUERETRO_NEOPIXEL
        range 1 255
        default 64
        help
            Caps per-channel brightness to limit glare and current draw.

```

- [ ] **Step 2: Commit**

```bash
git add main/Kconfig.projbuild
git commit -m "feat(neopixel): add Kconfig options (PS2-gated, pin, brightness)"
```

Verification happens in Task 4's build (the symbols must resolve and gate correctly).

---

### Task 4: Firmware glue, dependency, and wiring

Everything needed to compile and run the pixel on a PS2 build.

**Files:**
- Create: `main/idf_component.yml`
- Create: `main/system/neopixel.h`
- Create: `main/system/neopixel.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.c`

**Interfaces:**
- Consumes (Task 1): `neo_resolve_state()`, `neo_render()`, `enum neo_state`, `struct neo_rgb`.
- Consumes (Task 2): `err_led_get()`.
- Consumes (existing): `bt_hci_get_inquiry()` (`bluetooth/hci.h`), `bt_host_get_flag_dev_cnt()` + `BT_DEV_HID_INIT_DONE` (`bluetooth/host.h`).
- Produces: `void neopixel_init(void);`

- [ ] **Step 1: Declare the `led_strip` dependency**

Create `main/idf_component.yml`:

```yaml
dependencies:
  espressif/led_strip: "^3.0.3"
```

- [ ] **Step 2: Create the module header**

Create `main/system/neopixel.h`:

```c
/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _NEOPIXEL_H_
#define _NEOPIXEL_H_

void neopixel_init(void);

#endif /* _NEOPIXEL_H_ */
```

- [ ] **Step 3: Create the glue implementation**

Create `main/system/neopixel.c`:

```c
/*
 * Copyright (c) 2026, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "led_strip.h"
#include "sdkconfig.h"
#include "bluetooth/host.h"
#include "bluetooth/hci.h"
#include "led.h"
#include "neopixel.h"
#include "neopixel_anim.h"

#define NEO_FRAME_MS 33 /* ~30 fps */

static led_strip_handle_t neo_strip;

static void neopixel_task(void *param) {
    uint32_t frame = 0;
    (void)param;

    while (1) {
        bool err = err_led_get() != 0;
        uint32_t inquiry = bt_hci_get_inquiry();
        uint32_t cnt = bt_host_get_flag_dev_cnt(BT_DEV_HID_INIT_DONE);

        enum neo_state state = neo_resolve_state(err, inquiry, cnt);
        struct neo_rgb c = neo_render(state, cnt, frame, CONFIG_BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS);

        led_strip_set_pixel(neo_strip, 0, c.r, c.g, c.b);
        led_strip_refresh(neo_strip);

        frame++;
        vTaskDelay(NEO_FRAME_MS / portTICK_PERIOD_MS);
    }
}

void neopixel_init(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_BLUERETRO_NEOPIXEL_PIN,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = 0,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = 0,
        },
    };

    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &neo_strip) != ESP_OK) {
        /* NeoPixel is a cosmetic add-on; never block boot on it. */
        return;
    }
    led_strip_clear(neo_strip);

    xTaskCreatePinnedToCore(&neopixel_task, "neopixel_task", 2048, NULL, 5, NULL, 0);
}
```

- [ ] **Step 4: Register the sources (gated)**

In `main/CMakeLists.txt`, add after the `if(CONFIG_BLUERETRO_COVERAGE) ... endif()` block (after line 103, before `idf_component_register(`):

```cmake
if(CONFIG_BLUERETRO_NEOPIXEL)
    list(APPEND srcs
        "system/neopixel.c"
        "system/neopixel_anim.c"
    )
endif()
```

(No `REQUIRES` edit is needed: a managed dependency declared in `main/idf_component.yml` is auto-added to the `main` component, so `#include "led_strip.h"` resolves.)

- [ ] **Step 5: Call `neopixel_init()` from `main.c`**

In `main/main.c`, add near the other `system/*` includes at the top:

```c
#include "system/neopixel.h"
```

Then immediately after `err_led_init(chip_package);` (line 93), add:

```c
#ifdef CONFIG_BLUERETRO_NEOPIXEL
    neopixel_init();
#endif
```

- [ ] **Step 6: Build for a PS2 config with the NeoPixel enabled**

Run:
```bash
cp configs/hw2/playstation sdkconfig
printf '\nCONFIG_BLUERETRO_NEOPIXEL=y\nCONFIG_BLUERETRO_NEOPIXEL_PIN=4\nCONFIG_BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS=64\n' >> sdkconfig
idf.py reconfigure
idf.py build
```
Expected: the component manager fetches `espressif/led_strip (3.0.3)`; the build completes with `Project build complete`. `neopixel.c` and `neopixel_anim.c` compile and link.

- [ ] **Step 7: Sanity-check the gate (non-PS2 build unaffected)**

Run:
```bash
cp configs/hw2/genesis sdkconfig
idf.py reconfigure
idf.py build
```
Expected: build completes; `CONFIG_BLUERETRO_NEOPIXEL` is unset (Genesis ≠ PSX_PS2), so the new sources are not compiled. Restore the PS2 config afterward: `cp configs/hw2/playstation sdkconfig`.

- [ ] **Step 8: Commit**

```bash
git add main/idf_component.yml main/system/neopixel.h main/system/neopixel.c \
        main/CMakeLists.txt main/main.c
git commit -m "feat(neopixel): led_strip glue, task, and PS2 wiring"
```

---

### Task 5: Final verification & hardware bring-up checklist

**Files:**
- Create: `docs/superpowers/plans/neopixel-hardware-checklist.md`

- [ ] **Step 1: Re-run host unit tests (regression)**

Run: `tests/unit/run_neopixel_anim_tests.sh`
Expected: PASS — `All NeoPixel animation tests passed.`

- [ ] **Step 2: Full PS2 build green**

Run:
```bash
cp configs/hw2/playstation sdkconfig
printf '\nCONFIG_BLUERETRO_NEOPIXEL=y\nCONFIG_BLUERETRO_NEOPIXEL_PIN=4\nCONFIG_BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS=64\n' >> sdkconfig
idf.py reconfigure && idf.py build
```
Expected: `Project build complete`.

- [ ] **Step 3: Write the manual hardware checklist**

Create `docs/superpowers/plans/neopixel-hardware-checklist.md`:

```markdown
# NeoPixel bring-up checklist (PS2 build)

Wiring: WS2812 DIN -> GPIO4 (or configured pin); 5V + GND; ~300-500R in series
on DIN and a decoupling cap recommended.

- [ ] Power on, no controller: pixel breathes **amber** (never fully off).
- [ ] Enter pairing / put a controller in discoverable mode: pixel pulses **blue**.
- [ ] Connect 1 pad: pixel is **green** with a single heartbeat beat per ~2.5 s.
- [ ] Connect 2 pads (or multitap): green with a double-beat.
- [ ] Force an init fault (e.g. FS/NVS): pixel blinks **red** rapidly and latches.
- [ ] On-board error LED still pulses/behaves exactly as before (unchanged).
- [ ] PS2 controller input works normally throughout (no I/O regression).
```

- [ ] **Step 4: Commit and finish**

```bash
git add docs/superpowers/plans/neopixel-hardware-checklist.md
git commit -m "docs(neopixel): hardware bring-up checklist"
```

Then restore a clean working config: `git checkout sdkconfig 2>/dev/null || rm -f sdkconfig`.

---

## Self-Review

**1. Spec coverage:**
- Single WS2812, PS2-only → Task 3 (`depends on BLUERETRO_SYSTEM_PSX_PS2`), Task 4. ✓
- Four states (error/pairing/booting/connected+count) → Task 1 `neo_resolve_state`/`neo_render`, tested. ✓
- Balanced personality (colour=state, animation=flash; heartbeat beats = count) → Task 1 render + beat tests. ✓
- Reuse `led_strip`, `bt_hci_get_inquiry()`, `bt_host_get_flag_dev_cnt()`, `err_led_get()` → Tasks 2 & 4. ✓
- On-board LED unchanged → Task 2 only adds a read-only getter; no `err_led` path edited. ✓
- Core-0 task, RMT (free on PS2), GPIO4, brightness cap 64, off by default → Tasks 3 & 4. ✓
- Testing = host unit tests for pure logic + manual hardware checklist → Tasks 1 & 5. ✓

**2. Placeholder scan:** No TBD/TODO; every code and command step is concrete. ✓

**3. Type consistency:** `enum neo_state`, `struct neo_rgb`, `neo_resolve_state(bool,uint32_t,uint32_t)`, `neo_render(enum neo_state,uint32_t,uint32_t,uint8_t)`, `err_led_get(void)->uint32_t`, `bt_host_get_flag_dev_cnt(uint32_t)`, `bt_hci_get_inquiry(void)` — used identically in header, tests, and glue. ✓
```
