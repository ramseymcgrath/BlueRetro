# NeoPixel Status Indicator — Design

**Date:** 2026-07-08
**Target:** BlueRetro, PS2-exclusive build (`ps_spi.c` core), ESP32, ESP-IDF v5.5.0
**Status:** Approved (brainstorming)

## Summary

Add a single WS2812 ("NeoPixel") RGB LED as the **primary, visible** status
indicator for a PS2-only BlueRetro build. The existing on-board LEDs
(the LEDC error LED and the DualShock analog-mode LEDs) are hidden inside the
enclosure and are left **completely unchanged** — the NeoPixel is additive.

The pixel conveys four states with a "balanced" personality: status is always
legible at a glance (colour = state) while animation supplies the flash.

## Goals

- One WS2812 as the outward-facing status light for a PS2 build.
- Communicate four states: **Error**, **Pairing/discoverable**, **Booting/not-ready**, **Connected (+ pad count)**.
- Look lively — gentle ambient motion at rest, attention-grabbing on pairing/error.
- Reuse existing libraries and firmware signals; do **not** hand-roll a WS2812 driver or duplicate state tracking.
- Keep the existing on-board LED behaviour bit-for-bit identical.

## Non-Goals / Out of Scope

- Multiple pixels / strips / rings (single pixel only).
- Per-player physical mapping (a single pixel cannot show per-port; count is encoded instead).
- Reworking or replacing `err_led` or the analog-mode LEDs.
- Support for non-PS2 cores (gated off by config; other cores contend for RMT and pins and are explicitly excluded here).
- User-configurable colour themes (fixed palette v1).

## Constraints & Context (verified in-repo)

- **Peripheral budget on PS2:** both SPI hosts (HSPI=P1, VSPI=P2) are consumed by `ps_spi.c`; LEDC ch0/timer0 drives the error LED. **RMT is entirely free** on a PS2 build (only `nsi.c`/N64-GC and `sea_io.c`/Saturn use RMT, neither present here).
- **Free output-capable GPIOs on PS2:** 4, 13, 16, 18, 23. PS2 core already claims 5, 12, 15(analog LEDs), 17(err LED on non-SEA boards), 19, 21, 22, 25, 26, 27, 32, 33, 34. Input-only pins (34–39) cannot drive the data line.
- **Chosen pin:** GPIO4 (no strapping role, unused by the PS2 map). Configurable.
- **IDF v5.5.0** → modern `led_strip` RMT-device API; codebase already uses the v5 RMT driver (`rmt_symbol_word_t`, `RMTMEM` in `nsi.c`).
- **Flash headroom:** 1 MB app partitions (factory/ota_0/ota_1); `led_strip` adds a few KB — no concern.

## Architecture

New self-contained module in `main/system/`, mirroring the existing `err_led`
pattern:

- `main/system/neopixel.c`
- `main/system/neopixel.h`

Compiled only when `CONFIG_BLUERETRO_NEOPIXEL` is set. `neopixel_init()` is
called once from `main.c`, immediately after `err_led_init()`, behind an
`#ifdef`. It configures the strip and spawns one FreeRTOS task
**pinned to core 0** (alongside `err_led_task`), so the timing-critical PS2 SPI
ISRs on core 1 are never touched.

### Public API

```c
void neopixel_init(void);   /* configure led_strip + spawn render task */
```

`init`-only for v1 (the task self-drives by polling). No teardown path needed
(the adapter runs until power-off), matching `err_led`'s lifecycle.

### Driver (no hand-rolling)

Espressif's `led_strip` managed component provides all WS2812 timing, bit
encoding, colour-order handling and refresh over a free RMT channel. Added via a
new `main/idf_component.yml`:

```yaml
dependencies:
  espressif/led_strip: "^3"
```

Configuration:
- `led_strip_config_t`: `strip_gpio_num = CONFIG_BLUERETRO_NEOPIXEL_PIN`, `max_leds = 1`, `led_model = LED_MODEL_WS2812`, GRB colour order.
- `led_strip_rmt_config_t`: default RMT clock source, ~10 MHz resolution, `with_dma = false` (original ESP32 RMT has no DMA; one pixel does not need it).

Our code only computes an RGB value and calls `led_strip_set_pixel()` +
`led_strip_refresh()`.

## State Machine

Highest-priority active state wins each frame. Colour encodes the state;
animation encodes the flash.

| Priority | State | Source signal (existing) | Colour | Animation |
|---|---|---|---|---|
| 1 | Error / fault | `err_led` error latch via new read-only accessor | Red | Rapid blink (~5 Hz square) |
| 2 | Pairing / discoverable | `bt_hci_get_inquiry()` (hci.h) != 0 | Blue | Snappy pulse (~1.5 Hz breathe) — the attention-grabber |
| 3 | Booting / not-ready / idle | fallback: no pads connected, not pairing, no error | Amber | Slow breathe (~0.5 Hz) |
| 4 | Connected (+ count) | count of `bt_dev[]` with `BT_DEV_HID_INIT_DONE` set | Green | Gentle breathe + **N heartbeat beats per ~2.5 s cycle** (1 pad = single beat, 2 = double-beat, … up to 8) |

### Resolution logic (unambiguous ordering)

```
if (error_latched)            -> ERROR
else if (inquiry_active)      -> PAIRING
else if (connected_count > 0) -> CONNECTED(connected_count)
else                          -> BOOTING          /* also covers "up, idle, nothing connected" */
```

The BOOTING/amber state intentionally doubles as the idle-empty "waiting for a
controller" state — it matches the existing `err_led` "set while !bt_ready"
semantics. If the BT stack fails to initialise, `main.c` calls `err_led_set()`,
which raises the error latch → ERROR (red) takes priority, so amber never
masks a real fault.

### Palette & brightness

Fixed palette (pre-brightness-scaled 8-bit RGB): Red `(255,0,0)`, Blue
`(0,0,255)`, Amber `(255,120,0)`, Green `(0,255,0)`. All output is scaled by
`CONFIG_BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS` (default 64/255) to cap glare and
current draw. Breathe/beat curves use a small integer sine/triangle
approximation (application animation logic, not driver code).

## Data Flow

`neopixel_task` — core 0, priority 5 (same as `err_led_task`), ~2 KB stack,
~30 fps (`vTaskDelay(33 ms)`):

1. Read three existing signals: error latch, `bt_hci_get_inquiry()`, connected pad count.
2. `neo_resolve_state(err, inquiry, count) -> state` — **pure function**.
3. `neo_render(state, count, frame) -> rgb` — **pure function** of a monotonic frame counter.
4. `led_strip_set_pixel()` + `led_strip_refresh()`.

Splitting steps 2–3 into pure functions makes the priority ladder and the
per-frame colour/animation unit-testable without hardware.

CPU cost is negligible: one pixel refreshed at 30 fps on the BT core; the RMT
peripheral clocks the bits out in hardware.

## Reuse Ledger ("use existing code, avoid hand-rolling")

| Concern | Reused element |
|---|---|
| WS2812 timing / encoding / refresh | `espressif/led_strip` component |
| Pairing / discoverable state | existing `bt_hci_get_inquiry()` |
| Error state | existing `err_led` latch, exposed via a new 1-line `err_led_get()` accessor (behaviour of `err_led` unchanged) |
| Connected pad count | new small helper `bt_host_get_conn_dev_cnt()` following the exact `bt_dev[]` + `BT_DEV_HID_INIT_DONE` iteration idiom already in `manager.c` |
| Task creation / init | copied from `err_led_init()` (`xTaskCreatePinnedToCore(..., core 0)`) |

New code is limited to: the module itself, three thin read-only accessors, and
the animation math (unavoidable application logic — the driver is not
hand-rolled).

## Configuration (Kconfig — `main/Kconfig.projbuild`)

- `BLUERETRO_NEOPIXEL` — bool, default `n`. Master enable.
- `BLUERETRO_NEOPIXEL_PIN` — int, default `4`.
- `BLUERETRO_NEOPIXEL_MAX_BRIGHTNESS` — int (0–255), default `64`.

## Testing

- **Unit tests** (host-side, via the existing `main/tests` setup): `neo_resolve_state()` priority ordering across all input combinations; `neo_render()` returns the expected colour per state and a bounded, animated brightness across a frame sweep (including correct beat-count for 1..8 pads).
- **Manual hardware bring-up checklist:** power-on → amber breathe; enter pairing → blue pulse; connect 1/2 pads → green with correct beat count; force init error → red blink; confirm on-board LED still blinks unchanged; confirm PS2 controller I/O unaffected.

## Risks & Mitigations

- **Exact `err_led` flag polarity/semantics:** the read-only accessor's precise source (`led_flags`/`ERR_LED_SET`) is confirmed by reading `led.c` in full during implementation; the design only depends on "error condition is observable read-only without altering `err_led` behaviour."
- **GPIO4 board availability:** GPIO4 is free in the firmware pin map, but the physical board must route it out. Pin is Kconfig-configurable (13/16/18/23 fallbacks) if a given PCB differs.
- **Brightness/current:** capped via Kconfig default; single pixel at ≤64/255 is well within budget.

## Terminal step

On spec approval → invoke the `writing-plans` skill to produce the phased
implementation plan.
