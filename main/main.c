/**
 * @file main.c
 * @brief SM Domotica ESP32-P4 - Waveshare ESP32-P4-WiFi6-Touch-LCD-7B
 */

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"
#include "app_nav.h"
#include "wifi_manager.h"
#include "wifi_config_screen.h"
/* wifi_manager temporalmente deshabilitado */

static const char *TAG = "main";

static void status_task(void *arg)
{
    (void)arg;
    char date_buf[40];
    char time_buf[12];
    uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tick++;
        snprintf(date_buf, sizeof(date_buf), "Lun, 11 de mayo del 2026");
        snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu:%02lu",
                 (unsigned long)((tick / 3600) % 24),
                 (unsigned long)((tick / 60) % 60),
                 (unsigned long)(tick % 60));

        /* LVGL lock obligatorio - modifica objetos UI desde tarea externa */
        if (lvgl_port_lock(50)) {
            app_nav_set_wifi(false);
            app_nav_set_ha(false);
            app_nav_set_datetime(date_buf, time_buf);
            lvgl_port_unlock();
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SM Domotica - ESP32-P4 arrancando");

    /* 1. Inicializar display + touch + LVGL (Waveshare BSP) */
    bsp_display_start();
    ESP_LOGI(TAG, "Display inicializado");
    bsp_display_brightness_set(100);
    /* Tema basico sin sombras - evita crashes por circle cache */
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_basic_init(disp);
    lv_disp_set_theme(disp, th);  /* Backlight 100% */

    /* 2. Inicializar navegacion dentro del lock de LVGL */
    if (lvgl_port_lock(0)) {
        app_nav_init();
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "UI lista");

    /* 3. Tareas de fondo */
    /* wifi_manager_start(); // desactivado temporal - esp_hosted v1.4 API */
    xTaskCreate(status_task, "status", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
}
