// SPDX-License-Identifier: MIT
/*
 * Simple I2C example for SSD1306 driver
 * Initializes display, draws some primitives, and flushes to screen.
 */
#include "esp_err.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <ssd1306.h>

static const char *TAG = "SSD1306_I2C_EXAMPLE";

// Init i2c
static i2c_master_bus_handle_t i2c_bus0_init(gpio_num_t sda, gpio_num_t scl,
                                             uint32_t hz) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = sda,
        .scl_io_num                   = scl,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 0,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    // NOTE: per-device speed is set when adding the device (in driver bind).
    return bus;
}

void app_main(void) {
    i2c_master_bus_handle_t i2c_bus =
        i2c_bus0_init(GPIO_NUM_21, GPIO_NUM_22, 400000);

    ssd1306_config_t cfg = {
        .width  = 128,
        .height = 64,
        .fb     = NULL, // let driver allocate internally
        .fb_len = 0,
        .iface.i2c =
            {
                .port     = I2C_NUM_0,
                .addr     = 0x3C,        // typical SSD1306 I2C address
                .rst_gpio = GPIO_NUM_NC, // no reset pin
            },
    };

    ssd1306_handle_t d = NULL;
    ESP_ERROR_CHECK(ssd1306_new_i2c(&cfg, &d));

    // ----- Clear screen -----
    ESP_ERROR_CHECK(ssd1306_clear(d));

    // ----- Draw pixels in corners of screen -----
    ESP_ERROR_CHECK(ssd1306_draw_pixel(d, 0, 0, true));
    ESP_ERROR_CHECK(ssd1306_draw_pixel(d, cfg.width - 1, 0, true));
    ESP_ERROR_CHECK(ssd1306_draw_pixel(d, 0, cfg.height - 1, true));
    ESP_ERROR_CHECK(ssd1306_draw_pixel(d, cfg.width - 1, cfg.height - 1, true));

    // ----- Draw rectangles -----
    ESP_ERROR_CHECK(ssd1306_draw_rect(d, 2, 2, 40, 20, false));
    ESP_ERROR_CHECK(ssd1306_draw_rect(d, 2, 24, 32, 16, true));

    // ----- Draw circles -----
    ESP_ERROR_CHECK(ssd1306_draw_circle(d, 32, 52, 8, true));
    ESP_ERROR_CHECK(ssd1306_draw_circle(d, 100, 52, 4, false));

    // ----- Draw lines -----
    ESP_ERROR_CHECK(ssd1306_draw_line(d, 2, 2, 40, 20, true));
    ESP_ERROR_CHECK(ssd1306_draw_line(d, 32, 52, 100, 52, true));

    // ----- Draw text -----
    ESP_ERROR_CHECK(ssd1306_draw_text(d, 48, 2, "OK!", true));
    ESP_ERROR_CHECK(
        ssd1306_draw_text_scaled(d, 48, 10, "Hello\nWorld!", true, 2));

    ESP_ERROR_CHECK(ssd1306_display(d));

    ESP_LOGI(TAG, "Display updated successfully.");
}
