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
/* Always yield at least one tick, even if the tick period > NEO_FRAME_MS. */
#define NEO_FRAME_TICKS (pdMS_TO_TICKS(NEO_FRAME_MS) > 0 ? pdMS_TO_TICKS(NEO_FRAME_MS) : 1)
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
        vTaskDelay(NEO_FRAME_TICKS);
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
