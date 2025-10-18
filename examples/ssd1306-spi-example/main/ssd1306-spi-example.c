// SPDX-License-Identifier: MIT
/*
 * Simple SPI example for SSD1306 driver
 * Initializes display, draws some primitives, and flushes to screen.
 */
#include "driver/spi_master.h"
#include "esp_log.h"
#include "ssd1306.h"
#include <stdio.h>

static const char *TAG = "SSD1306_SPI_EXAMPLE";

// Main function
void app_main(void) {
    const spi_bus_config_t buscfg = {
        .mosi_io_num     = 23, // Adjust for your board
        .miso_io_num     = -1,
        .sclk_io_num     = 18,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 0,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ssd1306_config_t cfg = {
        .bus    = SSD1306_SPI,
        .width  = 128,
        .height = 64,
        .iface.spi =
            {
                .host     = SPI2_HOST,
                .cs_gpio  = 5,       // Chip-select pin
                .dc_gpio  = 16,      // Data/Command pin
                .rst_gpio = 17,      // Reset pin (GPIO_NUM_NC if tied high)
                .clk_hz   = 8000000, // 8 MHz
            },
        .fb     = NULL, // Let driver allocate framebuffer
        .fb_len = 0,
    };

    ssd1306_handle_t d = NULL;
    ESP_ERROR_CHECK(ssd1306_new_spi(&cfg, &d));

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
