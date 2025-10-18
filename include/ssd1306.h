// SPDX-License-Identifier: MIT
/*
 * ssd1306.h - SSD1306 driver header
 * Copyright (c) 2025 Jonathan Wåhrenberg
 */

#pragma once

#include <driver/gpio.h>
#include <driver/i2c_types.h>
#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported physical interface types.
 */
typedef enum {
    SSD1306_I2C = 0, /*!< I2C connection */
    SSD1306_SPI = 1, /*!< SPI connection */
} ssd1306_bus_t;

/**
 * @brief Bitmap font descriptor.
 *
 * Glyphs are stored column-major, one byte per column.
 * Bit0 = top pixel, bit(height-1) = bottom.
 */
typedef struct {
    uint8_t        width;  /*!< Glyph width in pixels (e.g. 5) */
    uint8_t        height; /*!< Glyph height in pixels (e.g. 7) */
    uint8_t        first;  /*!< First supported ASCII code (e.g. 32) */
    uint8_t        last;   /*!< Last supported ASCII code (e.g. 126) */
    const uint8_t *bitmap; /*!< Pointer to font bitmap data */
} ssd1306_font_t;

/**
 * @brief I2C interface configuration.
 */
typedef struct {
    i2c_port_num_t port;     /*!< I2C port number */
    gpio_num_t     rst_gpio; /*!< Optional reset GPIO (GPIO_NUM_NC if unused) */
    uint8_t        addr;     /*!< 7-bit I2C address (usually 0x3C or 0x3D) */
} ssd1306_i2c_cfg_t;

/**
 * @brief SPI interface configuration.
 */
typedef struct {
    int host;     /*!< SPI host ID (e.g. SPI2_HOST) */
    int cs_gpio;  /*!< Chip-select GPIO number */
    int dc_gpio;  /*!< Data/Command GPIO number (required) */
    int rst_gpio; /*!< Optional reset GPIO (GPIO_NUM_NC if unused) */
    int clk_hz;   /*!< SPI clock frequency in Hz (default ~8 MHz if 0) */
} ssd1306_spi_cfg_t;

/**
 * @brief Display configuration structure for initialization.
 */
typedef struct {
    union {
        ssd1306_i2c_cfg_t i2c; /*!< I2C configuration */
        ssd1306_spi_cfg_t spi; /*!< SPI configuration */
    } iface;

    uint8_t *fb; /*!< Optional framebuffer pointer (NULL to auto-allocate) */
    size_t   fb_len;      /*!< Framebuffer length in bytes */
    ssd1306_bus_t bus;    /*!< Selected bus type */
    uint16_t      width;  /*!< Display width in pixels */
    uint16_t      height; /*!< Display height in pixels */
} ssd1306_config_t;

/**
 * @brief Display handle type.
 */
typedef struct ssd1306_t *ssd1306_handle_t;

/**
 * @brief Create and initialize a new SSD1306 display on I2C.
 *
 * @param[in]  cfg Configuration parameters.
 * @param[out] out Returned display handle.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t ssd1306_new_i2c(const ssd1306_config_t *cfg, ssd1306_handle_t *out);

/**
 * @brief Create and initialize a new SSD1306 display on SPI.
 *
 * @param[in]  cfg Configuration parameters.
 * @param[out] out Returned display handle.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t ssd1306_new_spi(const ssd1306_config_t *cfg, ssd1306_handle_t *out);

/**
 * @brief Set the active font for text drawing.
 *
 * @param h Display handle.
 * @param font Font descriptor to use.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_set_font(ssd1306_handle_t h, const ssd1306_font_t *font);

/**
 * @brief Delete a display handle and free associated resources.
 *
 * @param h Display handle.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_del(ssd1306_handle_t h);

// ----- Drawing API -----

/**
 * @brief Clear the entire framebuffer (set all pixels off).
 *
 * @param h Display handle.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_clear(ssd1306_handle_t h);

/**
 * @brief Draw or clear a single pixel.
 *
 * @param h Display handle.
 * @param x X-coordinate
 * @param y Y-coordinate
 * @param on true to set pixel, false to clear.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_draw_pixel(ssd1306_handle_t h, int x, int y, bool on);

/**
 * @brief Draw a rectangle (filled or outline).
 *
 * @param h Display handle.
 * @param x Top left corner X-coordinate.
 * @param y Top left corner Y-coordinate.
 * @param w Rectangle width.
 * @param hgt Rectangle height.
 * @param fill true to fill, false to outline.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_draw_rect(ssd1306_handle_t h, int x, int y, int w, int hgt,
                            bool fill);

/**
 * @brief Draw a straight line between two points.
 *
 * @param h Display handle.
 * @param x0 Point 1 X-coordinate.
 * @param y0 Point 1 Y-coordinate.
 * @param x1 Point 2 X-coordinate.
 * @param y1 Point 2 Y-coordinate.
 * @param on true to set pixel, false to clear.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_draw_line(ssd1306_handle_t h, int x0, int y0, int x1, int y1,
                            bool on);

/**
 * @brief Draw a circle (filled or outline).
 *
 * @param h Display handle.
 * @param xc Center point X-coordinate.
 * @param yc Center point Y-coordinate.
 * @param r Circle radius.
 * @param fill true to fill, false to outline.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_draw_circle(ssd1306_handle_t h, int xc, int yc, int r,
                              bool fill);

/**
 * @brief Draw ASCII text using the current font, scale = 1.
 *
 * @param h Display handle.
 * @param x Top left X-coordinate.
 * @param y Top left Y-coordinate.
 * @param text Text to be displayed.
 * @param on true to set pixel, false to clear.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_draw_text(ssd1306_handle_t h, int x, int y, const char *text,
                            bool on);

/**
 * @brief Draw ASCII text with specified integer scale factor.
 *
 * @param h Display handle.
 * @param x Top left X-coordinate.
 * @param y Top left Y-coordinate.
 * @param text Text to be displayed.
 * @param on true to set pixel, false to clear.
 * @param scale Integer scale factor.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_draw_text_scaled(ssd1306_handle_t h, int x, int y,
                                   const char *txt, bool on, int scale);

/**
 * @brief Send the current framebuffer to the display (flush).
 *
 * @param h Display handle.
 * @return ESP_OK on success.
 */
esp_err_t ssd1306_display(ssd1306_handle_t h);

#ifdef __cplusplus
}
#endif
