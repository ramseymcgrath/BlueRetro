# NeoPixel Per-Port Revision — Implementation Plan

> **For agentic workers:** implement task-by-task with review between tasks (superpowers:subagent-driven-development). Steps use checkbox (`- [ ]`) syntax.

**Goal:** Convert the single global-status NeoPixel into TWO per-port pixels on the existing analog-mode LED pins (GPIO12/15), each reflecting its own PS2 port with analog mode as an accent.

**Architecture:** Keep the pure/glue split. `neopixel_anim` becomes per-port (`neo_resolve_port` + beat-free `neo_render`). `neopixel.c` drives two `led_strip` devices, reading per-port connection (`bt_host_get_active_dev_from_out_idx`) and per-port analog (`ps_get_analog_led`). `ps_spi.c` stops driving GPIO12/15 and instead records analog state for the getter.

**Tech Stack:** C, ESP-IDF v5.5.0, FreeRTOS, `espressif/led_strip` v3.0.3, host `cc` for unit tests.

## Global Constraints

- PS2 build only (`CONFIG_BLUERETRO_SYSTEM_PSX_PS2`); feature gated by `CONFIG_BLUERETRO_NEOPIXEL` (default n).
- Two strips on **GPIO12 (port 1)** and **GPIO15 (port 2)** — 2 RMT channels (free on PS2). The GPIO4 single pixel is removed.
- `ps_spi.c` core-1 ISR: the four `gpio_set_level_iram(...led_pin,…)` calls become `ps_analog_led[...] = …` writes; this must never ADD work/latency to the hot path. Leave the `led_pin` struct field and `P1/P2_ANALOG_LED_PIN` defines in place (dead but harmless) to avoid churn in the big `ps_ctrl_ports` initializer.
- Leave `err_led` and all other existing behavior unchanged.
- Reuse existing getters: `err_led_get()`, `bt_hci_get_inquiry()`, `bt_host_get_active_dev_from_out_idx(out_idx,&dev)` (returns >=0 when a HID-init device is mapped to that out_idx).
- Render task pinned to core 0, priority 5.
- Brightness cap `CONFIG_BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS` (default 64).
- No build in the dev environment — the CI `idf.py build` on `configs/hw2/playstation` +`CONFIG_BLUERETRO_NEOPIXEL=y` is the hard gate. Host unit tests (`cc`) cover the pure logic.
- Branch: `neopixel-status-led`. Commit after each task. License header on new/changed files: `Copyright (c) 2026, Jacques Gagnon` / `SPDX-License-Identifier: Apache-2.0`.

## File Structure

- **Rewrite** `main/system/neopixel_anim.h` / `.c` — per-port model.
- **Rewrite** `tests/unit/test_neopixel_anim.c` — per-port tests.
- **Modify** `main/wired/ps_spi.c` + `main/wired/ps_spi.h` — analog-state array + `ps_get_analog_led()` getter; swap 4 ISR GPIO writes.
- **Modify** `main/Kconfig.projbuild` — replace `..._PIN` with `..._P1_PIN` (12) and `..._P2_PIN` (15).
- **Rewrite** `main/system/neopixel.c` — two strips, per-port render loop.
- **Modify** `main/main.c` — move the gated `neopixel_init()` call to AFTER `wired_rtos_init()` (so `led_strip` claims 12/15 after all wired GPIO setup).
- **Modify** docs (spec already revised; update the v1 plan pointer + hardware checklist).

---

### Task A: Per-port pure logic + host tests (TDD)

**Files:** Rewrite `main/system/neopixel_anim.h`, `main/system/neopixel_anim.c`, `tests/unit/test_neopixel_anim.c`. Harness unchanged (`tests/unit/run_neopixel_anim_tests.sh`).

**Interfaces produced:**
- `enum neo_state { NEO_STATE_IDLE, NEO_STATE_CONN_DIGITAL, NEO_STATE_CONN_ANALOG, NEO_STATE_PAIRING, NEO_STATE_ERROR };`
- `struct neo_rgb { uint8_t r, g, b; };`
- `enum neo_state neo_resolve_port(bool error, bool pairing, bool connected, bool analog);`
- `struct neo_rgb neo_render(enum neo_state state, uint32_t frame, uint8_t max_bright);`

- [ ] **Step 1: Replace the test file** `tests/unit/test_neopixel_anim.c`:

```c
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
```

- [ ] **Step 2: Run tests, expect FAIL** (`tests/unit/run_neopixel_anim_tests.sh`) — old API (`neo_resolve_state`, beat args) no longer matches; compile/link errors expected.

- [ ] **Step 3: Replace** `main/system/neopixel_anim.h`:

```c
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
```

- [ ] **Step 4: Replace** `main/system/neopixel_anim.c`:

```c
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
```

- [ ] **Step 5: Run tests, expect PASS** — six `ok` lines + "All NeoPixel animation tests passed."
- [ ] **Step 6: Commit** — `git add main/system/neopixel_anim.* tests/unit/test_neopixel_anim.c && git commit -m "feat(neopixel): per-port state/animation logic + tests"`

---

### Task B: ps_spi analog-state getter (core-1 ISR swap)

**Files:** Modify `main/wired/ps_spi.c`, `main/wired/ps_spi.h`.

**Interfaces produced:** `uint32_t ps_get_analog_led(uint32_t port);` — 1 if that port's emulated pad is in analog mode, else 0.

- [ ] **Step 1: Add the state array.** In `main/wired/ps_spi.c`, near the other file-scope statics (after the `ps_ctrl_ports[]` definition), add:

```c
static volatile uint8_t ps_analog_led[PS_PORT_MAX];
```

- [ ] **Step 2: Swap the four ISR GPIO writes.** Replace each of the four lines that currently read `gpio_set_level_iram(ps_ctrl_ports[port->mt_first_port ? 1 : 0].led_pin, N);` with `ps_analog_led[port->mt_first_port ? 1 : 0] = N;` (keep `N` = 1 for the two analog-on sites, 0 for the two analog-off sites). Do not change surrounding lines. Leave the `led_pin` struct field and its two initializers as-is.

- [ ] **Step 3: Add the getter** at the end of `main/wired/ps_spi.c` (file scope):

```c
uint32_t ps_get_analog_led(uint32_t port) {
    return (port < PS_PORT_MAX) ? ps_analog_led[port] : 0;
}
```

- [ ] **Step 4: Declare it** in `main/wired/ps_spi.h` (with the other prototypes):

```c
uint32_t ps_get_analog_led(uint32_t port);
```

- [ ] **Step 5:** No build available — self-review that (a) exactly four ISR calls were swapped, (b) no other `gpio_set_level_iram` calls were touched, (c) `PS_PORT_MAX` is the array bound used everywhere. Commit — `git add main/wired/ps_spi.c main/wired/ps_spi.h && git commit -m "feat(ps): expose per-port analog-mode state, free GPIO12/15 for NeoPixel"`

---

### Task C: Two-strip glue + Kconfig + init placement

**Files:** Modify `main/Kconfig.projbuild`, rewrite `main/system/neopixel.c`, modify `main/main.c`.

- [ ] **Step 1: Kconfig — replace the single pin option.** In `main/Kconfig.projbuild`, replace the `config BLUERETRO_NEOPIXEL_PIN` block with:

```
    config BLUERETRO_NEOPIXEL_P1_PIN
        int "NeoPixel port 1 data GPIO"
        depends on BLUERETRO_NEOPIXEL
        default 12
        help
            WS2812 data GPIO for port 1 (reuses the analog-mode LED pin).

    config BLUERETRO_NEOPIXEL_P2_PIN
        int "NeoPixel port 2 data GPIO"
        depends on BLUERETRO_NEOPIXEL
        default 15
        help
            WS2812 data GPIO for port 2 (reuses the analog-mode LED pin).
```

(Leave `BLUERETRO_NEOPIXEL` and `BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS` unchanged.)

- [ ] **Step 2: Rewrite** `main/system/neopixel.c`:

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
#include "wired/ps_spi.h"
#include "led.h"
#include "neopixel.h"
#include "neopixel_anim.h"

#define NEO_FRAME_MS 33 /* ~30 fps */
#define NEO_PORT_MAX 2

static led_strip_handle_t neo_strip[NEO_PORT_MAX];
static const int neo_gpio[NEO_PORT_MAX] = {
    CONFIG_BLUERETRO_NEOPIXEL_P1_PIN,
    CONFIG_BLUERETRO_NEOPIXEL_P2_PIN,
};

static void neopixel_task(void *param) {
    uint32_t frame = 0;
    (void)param;

    while (1) {
        bool err = err_led_get() != 0;
        bool pairing = bt_hci_get_inquiry() != 0;

        for (uint32_t p = 0; p < NEO_PORT_MAX; p++) {
            if (!neo_strip[p]) {
                continue;
            }
            struct bt_dev *dev = NULL;
            bool connected = bt_host_get_active_dev_from_out_idx(p, &dev) >= 0;
            bool analog = ps_get_analog_led(p) != 0;

            enum neo_state state = neo_resolve_port(err, pairing, connected, analog);
            struct neo_rgb c = neo_render(state, frame, CONFIG_BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS);

            led_strip_set_pixel(neo_strip[p], 0, c.r, c.g, c.b);
            led_strip_refresh(neo_strip[p]);
        }

        frame++;
        vTaskDelay(NEO_FRAME_MS / portTICK_PERIOD_MS);
    }
}

static led_strip_handle_t neo_new_strip(int gpio) {
    led_strip_handle_t handle = NULL;
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
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

    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &handle) != ESP_OK) {
        return NULL;
    }
    led_strip_clear(handle);
    return handle;
}

void neopixel_init(void) {
    bool any = false;

    for (uint32_t p = 0; p < NEO_PORT_MAX; p++) {
        neo_strip[p] = neo_new_strip(neo_gpio[p]);
        if (neo_strip[p]) {
            any = true;
        }
    }
    if (!any) {
        /* NeoPixel is a cosmetic add-on; never block boot on it. */
        return;
    }
    xTaskCreatePinnedToCore(&neopixel_task, "neopixel_task", 2048, NULL, 5, NULL, 0);
}
```

- [ ] **Step 3: Move the init call in** `main/main.c`. Remove the existing `#ifdef CONFIG_BLUERETRO_NEOPIXEL / neopixel_init(); / #endif` block from just after `err_led_init(chip_package);`, and re-insert the identical block immediately AFTER the `wired_rtos_init();` call (so `led_strip` claims GPIO12/15 after all wired GPIO setup). Keep the `#include "system/neopixel.h"` where it is.

- [ ] **Step 4:** No build available — self-review: two strips created from the two CONFIG pins; per-port loop reads `bt_host_get_active_dev_from_out_idx(p,&dev)` and `ps_get_analog_led(p)`; include `"wired/ps_spi.h"` present; init call now after `wired_rtos_init()`. Commit — `git add main/Kconfig.projbuild main/system/neopixel.c main/main.c && git commit -m "feat(neopixel): drive two per-port strips on GPIO12/15"`

---

### Task D: Docs

**Files:** Modify `docs/superpowers/plans/neopixel-hardware-checklist.md` (and note the revision in the v1 plan header).

- [ ] **Step 1: Replace** `docs/superpowers/plans/neopixel-hardware-checklist.md` body with the per-port checklist:

```markdown
# NeoPixel bring-up checklist (PS2 build, per-port)

Wiring: WS2812 DIN(port1) -> GPIO12, DIN(port2) -> GPIO15; 5V + GND; ~300-500R
series on each DIN, decoupling cap recommended. These are the former analog-mode
LED pins (strapping pins — a first-frame boot glitch is possible, cosmetic).

- [ ] Power on, no controllers: both pixels breathe **amber**.
- [ ] Enter pairing: both pixels pulse **blue**.
- [ ] Connect a controller to port 1: pixel 1 turns **green**; pixel 2 stays amber.
- [ ] Toggle that pad to analog mode: pixel 1 goes **steady/bright green**; digital = gentle breathe.
- [ ] Connect a second controller to port 2: pixel 2 turns green independently.
- [ ] Force an init fault: both pixels blink **red** and latch.
- [ ] On-board error LED still behaves as before; PS2 controller I/O unaffected
      (analog-mode toggling still works over the wire even though the discrete LED is gone).
```

- [ ] **Step 2: Commit** — `git add docs/superpowers/plans/neopixel-hardware-checklist.md && git commit -m "docs(neopixel): per-port hardware checklist"`

---

## Self-Review

- Per-port model with global override → Task A `neo_resolve_port`, tested (priority + analog/digital distinction). ✓
- Two strips on 12/15, GPIO4 removed → Task C. ✓
- ps_spi frees 12/15 + preserves analog state → Task B (4 ISR swaps + getter). ✓
- Reused getters only (`err_led_get`, `bt_hci_get_inquiry`, `bt_host_get_active_dev_from_out_idx`, `ps_get_analog_led`). ✓
- Init ordering hazard neutralized by moving `neopixel_init()` after `wired_rtos_init()` → Task C Step 3. ✓
- Type consistency: `neo_resolve_port(bool,bool,bool,bool)`, `neo_render(enum,uint32_t,uint8_t)`, `ps_get_analog_led(uint32_t)->uint32_t` used identically across tasks. ✓
- Hard gate: CI `idf.py build` (unbuildable here). ✓
