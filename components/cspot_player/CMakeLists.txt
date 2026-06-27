idf_component_register(
    SRCS "cspot_player.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_http_server
        audio_feedback
        espressif__esp_codec_dev
        esp_netif
        log
        freertos
)

# Import cspot library
get_filename_component(CSPOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cspot/cspot" ABSOLUTE)
add_subdirectory(${CSPOT_DIR} ${CMAKE_CURRENT_BINARY_DIR}/cspot)
target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -fexceptions)

# Habilitar excepciones en bell (requerido por cspot)
if(TARGET bell)
    target_compile_options(bell PRIVATE -fexceptions -fno-rtti)
endif()
if(TARGET cspot)
    target_compile_options(cspot PRIVATE -fexceptions -fno-rtti)
endif()

