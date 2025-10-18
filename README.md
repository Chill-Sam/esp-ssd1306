# SSD1306 OLED Driver for ESP-IDF

A minimal and efficient SSD1306 OLED display driver for **ESP-IDF v5.3+**, supporting both **I2C** and **SPI** interfaces. Designed for clarity, maintainability, and ease of integration.

## Features

* Supports I2C and SPI communication
* Compatible with all standard SSD1306 resolutions
* Automatic or user-managed framebuffer
* Basic drawing primitives (pixel, line, rectangle, circle)
* 5x7 ASCII font with optional scaling
* Thread-safe with internal locking
* MIT licensed

## Example Usage (I2C)

```c
i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 21,
    .scl_io_num = 22,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_master_bus_handle_t bus_handle;
i2c_new_master_bus(&bus_cfg, &bus_handle);

ssd1306_config_t cfg = {
    .bus = SSD1306_I2C,
    .width = 128,
    .height = 64,
    .iface.i2c = {
        .port = I2C_NUM_0,
        .addr = 0x3C,
        .rst_gpio = 17,
    },
};

ssd1306_handle_t disp;
ssd1306_new_i2c(&cfg, &disp);
ssd1306_clear(disp);
ssd1306_draw_text(disp, 0, 0, "SSD1306 I2C", true);
ssd1306_display(disp);
```

## License

MIT License © 2025 Jonathan Wåhrenberg.
