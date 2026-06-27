idf_component_register(
    SRCS "ma_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        espressif__esp_websocket_client
        json
        freertos
        log
)
