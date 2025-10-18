// SPDX-License-Identifier: MIT
/*
 * ssd1306_i2c.c - I2C logic
 * Copyright (c) 2025 Jonathan Wåhrenberg
 */

#include "ssd1306_private.h"

#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_log.h>

#define SSD1306_CTRL_CMD  0x00
#define SSD1306_CTRL_DATA 0x40

static const char *TAG = "SSD1306_I2C";

typedef struct {
    i2c_master_dev_handle_t dev;
    i2c_port_num_t          port;
    gpio_num_t              rst_gpio;
    uint8_t                 addr;
} ssd1306_i2c_ctx_t;

// Forward declarations
static esp_err_t i2c_send_cmd(void *ctx, const uint8_t *cmd, size_t n);
static esp_err_t i2c_send_data(void *ctx, const uint8_t *data, size_t n);
static esp_err_t i2c_reset(void *ctx);

static const ssd1306_bus_vt_t VT_I2C = {
    .send_cmd  = i2c_send_cmd,
    .send_data = i2c_send_data,
    .reset     = i2c_reset,
};

esp_err_t ssd1306_bind_i2c(struct ssd1306_t *d, i2c_port_num_t port,
                           uint8_t addr, gpio_num_t rst_gpio) {
    ESP_RETURN_ON_FALSE(d, ESP_ERR_INVALID_ARG, TAG, "null dev");

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(port, &bus), TAG,
                        "I2C port %d not initialized", port);

    ssd1306_i2c_ctx_t *ctx = calloc(1, sizeof(*ctx));
    ESP_RETURN_ON_FALSE(ctx, ESP_ERR_NO_MEM, TAG, "no mem");
    ctx->port                   = port;
    ctx->addr                   = addr;
    ctx->rst_gpio               = rst_gpio;

    i2c_device_config_t dev_cfg = {
        .device_address = addr,
        .scl_speed_hz   = 400000,
        .scl_wait_us    = 0,
        .flags =
            {
                .disable_ack_check = 0,
            },
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &ctx->dev);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }

    if (rst_gpio != GPIO_NUM_NC) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << rst_gpio,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&io) == ESP_OK) {
            gpio_set_level(rst_gpio, 1);
        } else {
            ESP_LOGW(TAG,
                     "rst_gpio %d config failed; continuing without HW reset",
                     rst_gpio);
            ctx->rst_gpio = GPIO_NUM_NC;
        }
    }

    d->vt      = &VT_I2C;
    d->bus_ctx = ctx;

    return ESP_OK;
}

static esp_err_t i2c_send_cmd(void *ctx, const uint8_t *cmds, size_t n) {
    if (!n)
        return ESP_OK;
    ssd1306_i2c_ctx_t *c = ctx;

    // control + payload on stack for a single burst
    const size_t MAX = 32; // payload per burst
    size_t       off = 0;
    while (off < n) {
        size_t  blk = (n - off) > MAX ? MAX : (n - off);
        uint8_t buf[1 + MAX];
        buf[0] = SSD1306_CTRL_CMD;
        memcpy(&buf[1], &cmds[off], blk);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(c->dev, buf, 1 + blk, -1), TAG,
                            "cmd xfer");
        off += blk;
    }
    return ESP_OK;
}

static esp_err_t i2c_send_data(void *ctx, const uint8_t *data, size_t n) {
    if (!n)
        return ESP_OK;
    ssd1306_i2c_ctx_t *c   = ctx;

    const size_t       MAX = 32; // payload per burst
    size_t             off = 0;

    while (off < n) {
        size_t  blk = (n - off) > MAX ? MAX : (n - off);
        uint8_t buf[1 + MAX];
        buf[0] = SSD1306_CTRL_DATA;
        memcpy(&buf[1], &data[off], blk);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(c->dev, buf, 1 + blk, -1), TAG,
                            "data xfer");
        off += blk;
    }

    return ESP_OK;
}

static esp_err_t i2c_reset(void *ctx) {
    ssd1306_i2c_ctx_t *c = ctx;

    if (c->rst_gpio == GPIO_NUM_NC)
        return ESP_OK;

    gpio_set_level(c->rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(c->rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t ssd1306_unbind_i2c(struct ssd1306_t *d) {
    if (!d || !d->bus_ctx)
        return ESP_OK; // already unbound / never bound

    ssd1306_i2c_ctx_t *ctx = (ssd1306_i2c_ctx_t *)d->bus_ctx;
    esp_err_t          ret = ESP_OK;

    if (ctx->dev) {
        esp_err_t e = i2c_master_bus_rm_device(ctx->dev);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "i2c_master_bus_rm_device failed: %s",
                     esp_err_to_name(e));
            ret = e;
        }
        ctx->dev = NULL;
    }

    if (ctx->rst_gpio != GPIO_NUM_NC) {
        // Put it back to a neutral state (input, no pulls).
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << ctx->rst_gpio,
            .mode         = GPIO_MODE_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        (void)gpio_config(&io);
    }

    free(ctx);
    d->bus_ctx = NULL;
    d->vt      = NULL;

    return ret;
}
