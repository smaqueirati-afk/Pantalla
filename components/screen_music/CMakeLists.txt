idf_component_register(
    SRCS "screen_music.c"
    INCLUDE_DIRS "."
    REQUIRES
        ha_client
        lvgl__lvgl
        espressif__esp_lvgl_port
        ma_client
        spotify_client
        esp_http_client
        log
        freertos
        waveshare__esp32_p4_wifi6_touch_lcd_7b
    PRIV_REQUIRES
        esp_driver_jpeg
        volumio_client
        screen_audio
)

target_compile_options(__idf_screen_music PRIVATE -Wno-error=stringop-truncation -Wno-error=unused-variable)
target_compile_options(__idf_screen_music PRIVATE -Wno-format-truncation -Wno-stringop-truncation)
