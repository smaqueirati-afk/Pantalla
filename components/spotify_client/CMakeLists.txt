idf_component_register(
    SRCS "spotify_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_client
        json
        freertos
        log
        mbedtls
        esp-tls
)
