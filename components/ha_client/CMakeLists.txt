idf_component_register(
    SRCS "ha_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_websocket_client
        esp_http_client
        json
        nvs_flash
        freertos
        log
)
