idf_component_register(SRCS
    "main.c"
    "app_nav.c"
    "main_menu.c"
    "spotify_screen.c"
    "luces_screen.c"
    "wifi_manager.c"
    "wifi_config_screen.c"
    "audio_console.c"

    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_partition
        esp_http_server
        driver
        espressif__esp_hosted
        espressif__esp_wifi_remote
        espressif__esp_lvgl_port
        lvgl__lvgl
        waveshare__esp32_p4_wifi6_touch_lcd_7b
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
target_link_libraries(${COMPONENT_LIB} PRIVATE "-Wl,-u,g_wifi_osi_funcs")
target_link_libraries(${COMPONENT_LIB} PRIVATE "-Wl,-u,g_wifi_default_wpa_crypto_funcs")
