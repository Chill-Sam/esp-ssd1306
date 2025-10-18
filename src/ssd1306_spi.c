// SPDX-License-Identifier: MIT
/*
 * ssd1306_spi.c - SPI logic
 * Copyright (c) 2025 Jonathan Wåhrenberg
 */

#include "ssd1306_private.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_check.h>
#include <esp_log.h>
#include <string.h>

#define TAG "SSD1306_SPI"

// ---- Backend context ----
typedef struct {
    spi_device_handle_t dev;
    spi_host_device_t   host;
    gpio_num_t          dc_gpio;  // required for 4-wire SPI
    gpio_num_t          rst_gpio; // optional, GPIO_NUM_NC if not used
    int                 clk_hz;   // configured clock
} ssd1306_spi_ctx_t;

// ---- Forward declarations for vtable ----
static esp_err_t spi_send_cmd(void *ctx, const uint8_t *cmd, size_t n);
static esp_err_t spi_send_data(void *ctx, const uint8_t *data, size_t n);
static esp_err_t spi_reset(void *ctx);

// Vtable
static const ssd1306_bus_vt_t VT_SPI = {
    .send_cmd  = spi_send_cmd,
    .send_data = spi_send_data,
    .reset     = spi_reset,
};

// ---- DC handling via pre-transfer callback ----
// We pack the DC bit in transaction->user to avoid global state.
static inline void *pack_user(ssd1306_spi_ctx_t *c, int dc_bit) {
    return (void *)((uintptr_t)c | (uintptr_t)(dc_bit & 1));
}
static inline ssd1306_spi_ctx_t *unpack_ctx(void *u) {
    return (ssd1306_spi_ctx_t *)((uintptr_t)u & ~(uintptr_t)1);
}
static inline int     unpack_dc(void *u) { return (int)((uintptr_t)u & 1); }

static void IRAM_ATTR spi_pre_cb_set_dc(spi_transaction_t *t) {
    ssd1306_spi_ctx_t *c  = unpack_ctx(t->user);
    int                dc = unpack_dc(t->user);
    if (c->dc_gpio != GPIO_NUM_NC) {
        gpio_set_level(c->dc_gpio, dc);
    }
}

// ---- Small GPIO helpers ----
static inline void gpio_conf_output(gpio_num_t pin, int level) {
    if (pin == GPIO_NUM_NC)
        return;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
    (void)gpio_set_level(pin, level);
}

static inline void gpio_conf_disable(gpio_num_t pin) {
    if (pin == GPIO_NUM_NC)
        return;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode         = GPIO_MODE_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io);
}

esp_err_t ssd1306_bind_spi(struct ssd1306_t *d, spi_host_device_t host,
                           gpio_num_t cs_gpio, gpio_num_t dc_gpio,
                           gpio_num_t rst_gpio, int clk_hz) {
    ESP_RETURN_ON_FALSE(d, ESP_ERR_INVALID_ARG, TAG, "null dev");
    ESP_RETURN_ON_FALSE(dc_gpio != GPIO_NUM_NC, ESP_ERR_INVALID_ARG, TAG,
                        "D/C pin required");

    if (clk_hz <= 0)
        clk_hz = 8 * 1000 * 1000; // safe default 8 MHz

    // Configure control pins
    gpio_conf_output(dc_gpio, 0);
    gpio_conf_output(rst_gpio, 1);

    // Add a device on the already-initialized SPI bus
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = clk_hz,
        .mode           = 0, // SSD1306 = SPI mode 0
        .spics_io_num   = cs_gpio,
        .queue_size     = 2,
        .pre_cb         = spi_pre_cb_set_dc,
        .flags          = 0,
    };

    spi_device_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(host, &devcfg, &dev), TAG,
                        "spi_bus_add_device");

    // Allocate context
    ssd1306_spi_ctx_t *ctx = (ssd1306_spi_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        (void)spi_bus_remove_device(dev);
        return ESP_ERR_NO_MEM;
    }
    ctx->dev      = dev;
    ctx->host     = host;
    ctx->dc_gpio  = dc_gpio;
    ctx->rst_gpio = rst_gpio;
    ctx->clk_hz   = clk_hz;

    // Optional hardware reset pulse
    if (rst_gpio != GPIO_NUM_NC) {
        gpio_set_level(rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    d->vt      = &VT_SPI;
    d->bus_ctx = ctx;

    return ESP_OK;
}

// ---- Vtable methods ----
static esp_err_t spi_send_cmd(void *ctx_, const uint8_t *cmds, size_t n) {
    ssd1306_spi_ctx_t *ctx = (ssd1306_spi_ctx_t *)ctx_;
    if (!ctx || !cmds || n == 0)
        return ESP_OK;

    // Commands are tiny; still chunk conservatively.
    const size_t MAX = 32;
    size_t       off = 0;
    while (off < n) {
        size_t            blk = (n - off) > MAX ? MAX : (n - off);

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length    = (uint32_t)(blk * 8);
        t.tx_buffer = &cmds[off];
        t.user      = pack_user(ctx, 0); // D/C = 0 (command)

        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(ctx->dev, &t), TAG,
                            "cmd xfer");
        off += blk;
    }
    return ESP_OK;
}

static esp_err_t spi_send_data(void *ctx_, const uint8_t *data, size_t n) {
    ssd1306_spi_ctx_t *ctx = (ssd1306_spi_ctx_t *)ctx_;
    if (!ctx || !data || n == 0)
        return ESP_OK;

    // Framebuffer is ~1KB; 1024-byte chunks are fine.
    const size_t MAX = 1024;
    size_t       off = 0;
    while (off < n) {
        size_t            blk = (n - off) > MAX ? MAX : (n - off);

        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length    = (uint32_t)(blk * 8);
        t.tx_buffer = &data[off];
        t.user      = pack_user(ctx, 1); // D/C = 1 (data)

        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(ctx->dev, &t), TAG,
                            "data xfer");
        off += blk;
    }
    return ESP_OK;
}

static esp_err_t spi_reset(void *ctx_) {
    ssd1306_spi_ctx_t *ctx = (ssd1306_spi_ctx_t *)ctx_;
    if (!ctx)
        return ESP_OK;

    if (ctx->rst_gpio == GPIO_NUM_NC)
        return ESP_OK;

    gpio_set_level(ctx->rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(ctx->rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t ssd1306_unbind_spi(struct ssd1306_t *d) {
    if (!d || !d->bus_ctx)
        return ESP_OK;

    ssd1306_spi_ctx_t *ctx = (ssd1306_spi_ctx_t *)d->bus_ctx;
    esp_err_t          ret = ESP_OK;

    if (ctx->dev) {
        esp_err_t e = spi_bus_remove_device(ctx->dev);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "spi_bus_remove_device failed: %s",
                     esp_err_to_name(e));
            ret = e;
        }
        ctx->dev = NULL;
    }

    // Neutralize pins
    gpio_conf_disable(ctx->dc_gpio);
    gpio_conf_disable(ctx->rst_gpio);

    free(ctx);
    d->bus_ctx = NULL;
    d->vt      = NULL;

    return ret;
}
