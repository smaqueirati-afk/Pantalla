idf_component_register(
    SRCS "radio_player.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_client
        freertos
        log
        waveshare__esp32_p4_wifi6_touch_lcd_7b
        espressif__esp_codec_dev
        chmorgan__esp-libhelix-mp3
)
