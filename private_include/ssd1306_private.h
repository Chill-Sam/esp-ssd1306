// SPDX-License-Identifier: MIT
/*
 * ssd1306_private.h - Internal definitions (not part of public API)
 * Copyright (c) 2025 Jonathan Wåhrenberg
 */

#pragma once

#include "ssd1306.h"

#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Vtable struct
typedef struct {
    esp_err_t (*send_cmd)(void *ctx, const uint8_t *cmd, size_t n);
    esp_err_t (*send_data)(void *ctx, const uint8_t *data, size_t n);
    esp_err_t (*reset)(void *ctx);
} ssd1306_bus_vt_t;

// Struct representing physical SSD1306 display
struct ssd1306_t {
    const ssd1306_font_t *font;
    uint8_t              *fb;
    size_t                fb_len;

    // Bus-specific context and vtable
    void                   *bus_ctx;
    const ssd1306_bus_vt_t *vt;

    // Internal concurrency protection
    SemaphoreHandle_t lock;

    ssd1306_bus_t     bus;
    uint16_t          width;
    uint16_t          height;
    uint16_t          dx0, dy0, dx1, dy1;
    bool              dirty;
    bool              driver_owns_fb;
    bool              initialized;
};

// I2C functions
esp_err_t ssd1306_bind_i2c(struct ssd1306_t *d, i2c_port_num_t port,
                           uint8_t addr, gpio_num_t rst_gpio);
esp_err_t ssd1306_unbind_i2c(struct ssd1306_t *d);

// SPI functions
esp_err_t ssd1306_bind_spi(struct ssd1306_t *d, spi_host_device_t host,
                           gpio_num_t cs_gpio, gpio_num_t dc_gpio,
                           gpio_num_t rst_gpio, int clk_hz);
esp_err_t ssd1306_unbind_spi(struct ssd1306_t *d);

#ifdef __cplusplus
}
#endif
