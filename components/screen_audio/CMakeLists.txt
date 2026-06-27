idf_component_register(
    SRCS "screen_audio.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl__lvgl espressif__esp_lvgl_port bsp_extra ha_client volumio_client
)
