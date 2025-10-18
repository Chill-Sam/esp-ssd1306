idf_component_register(
                    SRCS "src/ssd1306_core.c" "src/ssd1306_i2c.c" "src/ssd1306_spi.c" "src/ssd1306_font.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_spi

)
