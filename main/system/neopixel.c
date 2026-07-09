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
