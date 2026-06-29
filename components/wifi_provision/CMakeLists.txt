idf_component_register(
    SRCS "wifi_provision.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_http_server
        esp_netif
        nvs_flash
        esp_event
        log
        freertos
        esp_hosted
        esp_driver_sdmmc
        esp_driver_sdspi
    EMBED_FILES "network_adapter.bin"
)
