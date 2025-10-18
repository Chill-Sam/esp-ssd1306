// SPDX-License-Identifier: MIT
/*
 * ssd1306_core.c - Core driver logic
 * Copyright (c) 2025 Jonathan Wåhrenberg
 */

#include "ssd1306.h"
#include "ssd1306_font.h"
#include "ssd1306_private.h"

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>

#define LOCK(d)           xSemaphoreTake((d)->lock, portMAX_DELAY)
#define UNLOCK(d)         xSemaphoreGive((d)->lock)
#define FB_LEN(w, h)      ((size_t)(((w) * (h)) / 8))
#define SSD1306_TEXT_HSPC 1
#define SSD1306_TEXT_VSPC 2

static const char *TAG = "SSD1306";

// ----- Helper functions -----
// Get framebuffer index
static inline size_t fb_index(const struct ssd1306_t *d, int x, int page) {
    // 1bpp, page-packed (8 vertical pixels per byte)
    return (size_t)page * d->width + (size_t)x;
}

// Send select window command
static esp_err_t set_window(struct ssd1306_t *d, uint8_t x0, uint8_t x1,
                            uint8_t p0, uint8_t p1) {
    const uint8_t cmds[] = {
        0x21,
        (uint8_t)(x0),
        (uint8_t)(x1), // COLUMNADDR
        0x22,
        p0,
        p1, // PAGEADDR
    };
    return d->vt->send_cmd(d->bus_ctx, cmds, sizeof(cmds));
}

static inline void dirty_reset(struct ssd1306_t *d) {
    d->dirty = false;
    d->dx0 = d->dy0 = INT16_MAX;
    d->dx1 = d->dy1 = -1;
}

// Mark bounding box as dirty
static inline void mark_dirty(struct ssd1306_t *d, int x0, int y0, int x1,
                              int y1) {
    if (!d->dirty) {
        d->dirty = true;
        d->dx0   = x0;
        d->dy0   = y0;
        d->dx1   = x1;
        d->dy1   = y1;
        return;
    }
    if (x0 < d->dx0)
        d->dx0 = x0;
    if (y0 < d->dy0)
        d->dy0 = y0;
    if (x1 > d->dx1)
        d->dx1 = x1;
    if (y1 > d->dy1)
        d->dy1 = y1;
}

// Draw a pixel directly into framebuffer (no checks)
// Preconditions:
//   - d != NULL
//   - 0 <= x < d->width
//   - 0 <= y < d->height
//   - framebuffer allocated
//   - device initialized
static inline void draw_pixel_fast(struct ssd1306_t *d, int x, int y, bool on) {
    const int     page = y >> 3; // 8 vertical pixels per byte
    const uint8_t mask = (uint8_t)(1u << (y & 7));
    uint8_t      *byte = &d->fb[(page * d->width) + x];

    if (on)
        *byte |= mask;
    else
        *byte &= (uint8_t)~mask;
}

// Draw a horizontal line [x0..x1] at y with clipping, using draw_pixel_fast().
// Requires: lock is held.
static inline void draw_hline_clipped(struct ssd1306_t *d, int x0, int x1,
                                      int y) {
    int w = (int)d->width;
    if (y < 0 || y >= (int)d->height)
        return;
    if (x0 > x1) {
        int t = x0;
        x0    = x1;
        x1    = t;
    }
    if (x1 < 0 || x0 >= w)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= w)
        x1 = w - 1;
    for (int x = x0; x <= x1; ++x) {
        draw_pixel_fast(d, x, y, true);
    }
}

// Plot a pixel with bounds guard, lock is held.
static inline void plot_if_visible(struct ssd1306_t *d, int x, int y) {
    if ((unsigned)x < d->width && (unsigned)y < d->height)
        draw_pixel_fast(d, x, y, true);
}

esp_err_t ssd1306_clear(ssd1306_handle_t h) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }
    memset(d->fb, 0, d->fb_len);
    mark_dirty(d, 0, 0, d->width - 1, d->height - 1);

    UNLOCK(d);
    return ESP_OK;
}

// Send initialization sequence
static esp_err_t run_init_sequence(struct ssd1306_t *d) {
    uint8_t compins;
    switch (d->height) {
    case 16:
    case 32:
        compins = 0x02;
        break;
    case 48:
    case 64:
        compins = 0x12;
        break;
    default:
        compins = 0x12;
        break;
    }

    const uint8_t init[] = {
        0xAE,       // DISPLAYOFF
        0x20, 0x00, // MEMORYMODE: horizontal
        0xA8, (uint8_t)(d->height - 1),
        0xD3, 0x00,    // DISPLAYOFFSET = 0
        0x40,          // STARTLINE(0)
        0xA1,          // SEGREMAP
        0xC8,          // COMSCANDEC
        0xDA, compins, // COMPINS
        0x81, 0x7F,    // CONTRAST
        0xA4,          // RESUME display
        0xA6,          // NORMALDISPLAY
        0xD5, 0x80,    // CLOCKDIV
        0xD9, 0xF1,    // PRECHARGE (0x22 for external VCC)
        0xDB, 0x40,    // VCOMDETECT
        0x8D, 0x14,    // CHARGEPUMP ON (0x10 for external VCC)
        0xAF           // DISPLAYON
    };

    return d->vt->send_cmd(d->bus_ctx, init, sizeof(init));
}

// Basic validation for config
static esp_err_t validate_cfg(const ssd1306_config_t *cfg) {
    if (!cfg)
        return ESP_ERR_INVALID_ARG;
    if (!cfg->width || !cfg->height)
        return ESP_ERR_INVALID_ARG;
    if (cfg->fb && cfg->fb_len != FB_LEN(cfg->width, cfg->height))
        return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

// Common creation for ssd1306_handle_t
static esp_err_t new_common(const ssd1306_config_t *cfg, ssd1306_handle_t *out,
                            struct ssd1306_t **dev_out) {
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "out=NULL");
    ESP_RETURN_ON_ERROR(validate_cfg(cfg), TAG, "bad cfg");

    struct ssd1306_t *d = calloc(1, sizeof(*d));
    ESP_RETURN_ON_FALSE(d, ESP_ERR_NO_MEM, TAG, "no memory");

    d->width  = cfg->width;
    d->height = cfg->height;

    d->fb_len = cfg->fb ? cfg->fb_len : FB_LEN(cfg->width, cfg->height);
    d->fb     = cfg->fb ? cfg->fb : calloc(1, d->fb_len);
    if (!d->fb) {
        free(d);
        return ESP_ERR_NO_MEM;
    }
    d->driver_owns_fb = (cfg->fb == NULL);

    d->lock           = xSemaphoreCreateMutex();
    if (!d->lock) {
        if (d->driver_owns_fb)
            free(d->fb);
        free(d);
        return ESP_ERR_NO_MEM;
    }

    d->font = &ssd1306_font5x7;

    *out    = d;
    if (dev_out)
        *dev_out = d;
    return ESP_OK;
}

// ----- Public API -----
esp_err_t ssd1306_new_i2c(const ssd1306_config_t *cfg, ssd1306_handle_t *out) {
    struct ssd1306_t *d = NULL;
    ESP_RETURN_ON_ERROR(new_common(cfg, out, &d), TAG, "alloc");

    ESP_RETURN_ON_ERROR(ssd1306_bind_i2c(d, cfg->iface.i2c.port,
                                         cfg->iface.i2c.addr,
                                         cfg->iface.i2c.rst_gpio),
                        TAG, "bind i2c");
    if (d->vt->reset)
        ESP_RETURN_ON_ERROR(d->vt->reset(d->bus_ctx), TAG, "reset");
    ESP_RETURN_ON_ERROR(run_init_sequence(d), TAG, "init seq");

    d->initialized = true;
    return ESP_OK;
}

esp_err_t ssd1306_new_spi(const ssd1306_config_t *cfg, ssd1306_handle_t *out) {
    struct ssd1306_t *d = NULL;
    ESP_RETURN_ON_ERROR(new_common(cfg, out, &d), TAG, "alloc");

    ESP_RETURN_ON_ERROR(
        ssd1306_bind_spi(d, cfg->iface.spi.host, cfg->iface.spi.cs_gpio,
                         cfg->iface.spi.dc_gpio, cfg->iface.spi.rst_gpio,
                         cfg->iface.spi.clk_hz),
        TAG, "bind spi");
    if (d->vt->reset)
        ESP_RETURN_ON_ERROR(d->vt->reset(d->bus_ctx), TAG, "reset");
    ESP_RETURN_ON_ERROR(run_init_sequence(d), TAG, "init seq");

    d->initialized = true;
    return ESP_OK;
}

esp_err_t ssd1306_set_font(ssd1306_handle_t h, const ssd1306_font_t *font) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }
    d->font = font; // may be NULL; draw_text will error if NULL
    UNLOCK(d);
    return ESP_OK;
}

esp_err_t ssd1306_del(ssd1306_handle_t h) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_ARG;

    LOCK(d);
    d->initialized = false;

    // Stop talking to the device first.
    if (d->bus == SSD1306_I2C) {
        (void)ssd1306_unbind_i2c(d);
    } else if (d->bus == SSD1306_SPI) {
        (void)ssd1306_unbind_spi(d);
    } else {
        ESP_LOGE(TAG, "Invalid bus: %d", d->bus);
    }

    if (d->driver_owns_fb && d->fb)
        free(d->fb);

    UNLOCK(d);
    vSemaphoreDelete(d->lock);
    free(d);

    return ESP_OK;
}

// ----- Drawing API -----

esp_err_t ssd1306_draw_pixel(ssd1306_handle_t h, int x, int y, bool on) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;
    if ((unsigned)x >= d->width || (unsigned)y >= d->height)
        return ESP_ERR_INVALID_ARG;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }
    draw_pixel_fast(d, x, y, on);
    mark_dirty(d, x, y, x, y);
    UNLOCK(d);

    return ESP_OK;
}

esp_err_t ssd1306_draw_rect(ssd1306_handle_t h, int x, int y, int w, int hgt,
                            bool fill) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;
    if (w <= 0 || hgt <= 0)
        return ESP_ERR_INVALID_ARG;

    // --- clip ---
    int x0 = x, y0 = y, x1 = x + w - 1, y1 = y + hgt - 1;
    if (x0 >= d->width || y0 >= d->height || x1 < 0 || y1 < 0)
        return ESP_OK;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= (int)d->width)
        x1 = (int)d->width - 1;
    if (y1 >= (int)d->height)
        y1 = (int)d->height - 1;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }

    if (!fill) {
        // top/bottom horizontal edges
        for (int xx = x0; xx <= x1; ++xx) {
            draw_pixel_fast(d, xx, y0, true);
            draw_pixel_fast(d, xx, y1, true);
        }
        // left/right vertical edges
        for (int yy = y0; yy <= y1; ++yy) {
            draw_pixel_fast(d, x0, yy, true);
            draw_pixel_fast(d, x1, yy, true);
        }
        mark_dirty(d, x0, y0, x1, y1);
        UNLOCK(d);
        return ESP_OK;
    }

    // --- filled: page-aware fill ---
    const int     first_page = y0 >> 3;
    const int     last_page  = y1 >> 3;

    const uint8_t first_mask =
        (uint8_t)(0xFFu << (y0 & 7)); // bits from y0%8 to 7
    const uint8_t last_mask =
        (uint8_t)(0xFFu >> (7 - (y1 & 7))); // bits from 0 to y1%8

    for (int page = first_page; page <= last_page; ++page) {
        const size_t row_base   = fb_index(d, x0, page);
        const int    bytes_wide = (x1 - x0 + 1);

        if (first_page == last_page) {
            // all inside one page → single mask
            const uint8_t mask = (uint8_t)(first_mask & last_mask);
            for (int i = 0; i < bytes_wide; ++i)
                d->fb[row_base + i] |= mask;
        } else if (page == first_page) {
            for (int i = 0; i < bytes_wide; ++i)
                d->fb[row_base + i] |= first_mask;
        } else if (page == last_page) {
            for (int i = 0; i < bytes_wide; ++i)
                d->fb[row_base + i] |= last_mask;
        } else {
            // middle pages fully covered → write 0xFF
            memset(&d->fb[row_base], 0xFF, (size_t)bytes_wide);
        }
    }

    mark_dirty(d, x0, y0, x1, y1);
    UNLOCK(d);
    return ESP_OK;
}

esp_err_t ssd1306_draw_line(ssd1306_handle_t h, int x0, int y0, int x1, int y1,
                            bool on) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;

    // Trivial reject if completely outside and not crossing
    if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) ||
        (x0 >= (int)d->width && x1 >= (int)d->width) ||
        (y0 >= (int)d->height && y1 >= (int)d->height)) {
        return ESP_OK;
    }

    int bx0 = (x0 < x1) ? x0 : x1;
    int by0 = (y0 < y1) ? y0 : y1;
    int bx1 = (x0 > x1) ? x0 : x1;
    int by1 = (y0 > y1) ? y0 : y1;

    // Integer Bresenham
    int dx  = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx  = (x0 < x1) ? 1 : -1;
    int dy  = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sy  = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }

    while (1) {
        draw_pixel_fast(d, x0, y0, on);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = err << 1; // 2*err
        if (e2 > -dy) {    // step in x
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) { // step in y
            err += dx;
            y0 += sy;
        }
    }

    mark_dirty(d, bx0, by0, bx1, by1);
    UNLOCK(d);
    return ESP_OK;
}

esp_err_t ssd1306_draw_circle(ssd1306_handle_t h, int xc, int yc, int r,
                              bool fill) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;
    if (r < 0)
        return ESP_ERR_INVALID_ARG;

    // Degenerate radius
    if (r == 0) {
        return ssd1306_draw_pixel(h, xc, yc, true);
    }

    // Precompute bbox for dirty marking
    int bx0 = xc - r;
    int by0 = yc - r;
    int bx1 = xc + r;
    int by1 = yc + r;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }

    // Midpoint circle algorithm
    int x   = r;
    int y   = 0;
    int err = 1 - r;

    if (!fill) {
        // Outline: 8-way symmetry
        while (x >= y) {
            plot_if_visible(d, xc + x, yc + y);
            plot_if_visible(d, xc + y, yc + x);
            plot_if_visible(d, xc - y, yc + x);
            plot_if_visible(d, xc - x, yc + y);
            plot_if_visible(d, xc - x, yc - y);
            plot_if_visible(d, xc - y, yc - x);
            plot_if_visible(d, xc + y, yc - x);
            plot_if_visible(d, xc + x, yc - y);

            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    } else {
        // Filled: draw horizontal spans between symmetric x-pairs
        while (x >= y) {
            // Four spans (top/bottom at +/-y, and at +/-x)
            draw_hline_clipped(d, xc - x, xc + x, yc + y);
            draw_hline_clipped(d, xc - x, xc + x, yc - y);
            draw_hline_clipped(d, xc - y, xc + y, yc + x);
            draw_hline_clipped(d, xc - y, xc + y, yc - x);

            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    }

    // One bbox mark is sufficient
    mark_dirty(d, bx0, by0, bx1, by1);

    UNLOCK(d);
    return ESP_OK;
}

esp_err_t ssd1306_draw_text(ssd1306_handle_t h, int x, int y, const char *text,
                            bool on) {
    return ssd1306_draw_text_scaled(h, x, y, text, on, 1);
}

esp_err_t ssd1306_draw_text_scaled(ssd1306_handle_t h, int x, int y,
                                   const char *text, bool on, int scale) {
    struct ssd1306_t *d = h;
    if (!d || !text)
        return ESP_ERR_INVALID_STATE;
    if (scale < 1)
        scale = 1;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }
    if (!d->font) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }

    const ssd1306_font_t *f     = d->font;
    const int             gw    = (int)f->width;
    const int             gh    = (int)f->height;

    int                   cur_x = x;
    int                   cur_y = y;

    int bx0 = cur_x, by0 = cur_y, bx1 = cur_x - 1, by1 = cur_y - 1;

    for (const char *p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\r')
            continue;
        if (ch == '\n') {
            cur_x = x;
            cur_y += (gh * scale) + SSD1306_TEXT_VSPC;
            continue;
        }

        if (ch < f->first || ch > f->last) {
            cur_x += (gw * scale) + SSD1306_TEXT_HSPC;
            continue;
        }

        const uint8_t *glyph = &f->bitmap[(ch - f->first) * gw];

        for (int cx = 0; cx < gw; ++cx) {
            uint8_t col = glyph[cx];
            if (!col)
                continue;
            for (int ry = 0; ry < gh; ++ry) {
                if (col & (1u << ry)) {
                    int base_x = cur_x + cx * scale;
                    int base_y = cur_y + ry * scale;
                    // scale block fill
                    for (int sx = 0; sx < scale; ++sx) {
                        for (int sy = 0; sy < scale; ++sy) {
                            int px = base_x + sx;
                            int py = base_y + sy;
                            if ((unsigned)px < d->width &&
                                (unsigned)py < d->height)
                                draw_pixel_fast(d, px, py, on);
                        }
                    }
                }
            }
        }

        cur_x += (gw * scale) + SSD1306_TEXT_HSPC;

        int gx1 = cur_x - 1;
        int gy1 = cur_y + (gh * scale) - 1;
        if (gx1 > bx1)
            bx1 = gx1;
        if (gy1 > by1)
            by1 = gy1;
    }

    if (bx1 >= bx0 && by1 >= by0)
        mark_dirty(d, bx0, by0, bx1, by1);

    UNLOCK(d);
    return ESP_OK;
}

esp_err_t ssd1306_display(ssd1306_handle_t h) {
    struct ssd1306_t *d = h;
    if (!d)
        return ESP_ERR_INVALID_STATE;

    LOCK(d);
    if (!d->initialized) {
        UNLOCK(d);
        return ESP_ERR_INVALID_STATE;
    }

    if (!d->driver_owns_fb) {
        // full flush
        const uint8_t p0 = 0, p1 = (uint8_t)((d->height >> 3) - 1);
        esp_err_t     err = set_window(d, 0, (uint8_t)(d->width - 1), p0, p1);
        if (err == ESP_OK)
            err = d->vt->send_data(d->bus_ctx, d->fb, d->fb_len);
        dirty_reset(d);
        UNLOCK(d);
        return err;
    }

    // partial flush using bbox; no-op if nothing dirty
    if (!d->dirty) {
        UNLOCK(d);
        return ESP_OK;
    }
    int x0 = d->dx0, y0 = d->dy0, x1 = d->dx1, y1 = d->dy1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 >= (int)d->width)
        x1 = d->width - 1;
    if (y1 >= (int)d->height)
        y1 = d->height - 1;

    uint8_t   p0 = (uint8_t)(y0 >> 3), p1 = (uint8_t)(y1 >> 3);
    esp_err_t err = set_window(d, (uint8_t)x0, (uint8_t)x1, p0, p1);
    if (err == ESP_OK) {
        const int bytes_wide = (x1 - x0 + 1);
        for (uint8_t p = p0; p <= p1; ++p) {
            const uint8_t *row = &d->fb[(size_t)p * d->width + (size_t)x0];
            err = d->vt->send_data(d->bus_ctx, row, (size_t)bytes_wide);
            if (err != ESP_OK)
                break;
        }
    }
    if (err == ESP_OK)
        dirty_reset(d);
    UNLOCK(d);
    return err;
}
