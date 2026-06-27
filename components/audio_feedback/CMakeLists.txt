idf_component_register(
    SRCS "audio_feedback.c"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
        log
        waveshare__esp32_p4_wifi6_touch_lcd_7b
        espressif__esp_codec_dev
)
