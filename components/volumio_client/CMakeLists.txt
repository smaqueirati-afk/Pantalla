idf_component_register(
    SRCS "volumio_client.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client json lwip freertos screen_music ma_client
)
