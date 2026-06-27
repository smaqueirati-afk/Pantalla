idf_component_register(
    SRCS "snapcast_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_netif
        lwip
        json
        freertos
        log
        esp_hw_support
    PRIV_REQUIRES
        audio_feedback
        espressif__esp_codec_dev
)
