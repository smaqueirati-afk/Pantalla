/**
 * @file main.c
 * @brief SM Domotica ESP32-P4 - Waveshare ESP32-P4-WiFi6-Touch-LCD-7B
 */

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"
#include "app_nav.h"
#include "wifi_manager.h"
#include "wifi_config_screen.h"
#include "wifi_provision.h"
#include "ha_client.h"
#include "esp_netif.h"
#include <string.h>
/* wifi_manager temporalmente deshabilitado */

static const char *TAG = "main";

/* ====== Home Assistant ====== */
#define HA_HOST  "192.168.1.29"
#define HA_PORT  8123
#define HA_TOKEN "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIwNTljMjQ1ODJiNTM0MTJhODU0NTIxNDMxMGZmNjFjMyIsImlhdCI6MTc4Mjc1MDk4MywiZXhwIjoyMDk4MTEwOTgzfQ.CiNppjNGKZM0C5v9Q78oawks2RalggVRKmQO9ZlPWd4"   /* <-- token NUEVO de HA */

/* Se llama cuando el WebSocket se autentica (o se cae) con HA */
static void on_ha_connected(bool connected)
{
    if (connected) ESP_LOGI(TAG, "### HA CONECTADO ###");
    else           ESP_LOGW(TAG, "### HA DESCONECTADO ###");
}

/* Se llama por cada estado que HA envia. Por ahora solo logueamos. */
static void on_ha_state(const ha_entity_t *e)
{
    if (!e) return;
    ESP_LOGI(TAG, "HA estado: %s = %s (%s)",
             e->entity_id, e->state, e->friendly_name);
}

/* ====== Mapeo de las 6 luces de la pantalla a entidades de HA ======
 * El indice coincide con LIGHT_NAMES[] de luces_screen.c:
 *   0 Tubo de LED, 1 Luz Dicroicas, 2 Luz Escritorio,
 *   3 Tira de LED, 4 Luz de Abajo,  5 Luz del Medio
 */
static const char *LUZ_ENTITY[6] = {
    "switch.luz_oficina_interruptor_1",                       /* 0 Tubo de LED        */
    "switch.luz_oficina_interruptor_2",                       /* 1 Dicroicas+Escrit.  */
    "light.pc",                                               /* 2 Luz de Arriba      */
    "light.efectos_de_luz_tira_de_led_tira_led_oficina",      /* 3 Tira de LED        */
    "light.tv_1",                                             /* 4 Luz de Abajo       */
    "light.tv",                                               /* 5 Luz del Medio      */
};

/* Entidad EXTRA para botones que controlan 2 luces a la vez. NULL = solo una. */
static const char *LUZ_ENTITY_EXTRA[6] = {
    NULL,
    "switch.luz_oficina_interruptor_3",  /* 1 ademas dispara Escritorio */
    NULL, NULL, NULL, NULL,
};

/* Manda turn_on/off de UNA entidad a HA */
static void luz_enviar(const char *entity, bool state)
{
    char domain[16] = {0};
    const char *dot = strchr(entity, '.');
    if (!dot || (size_t)(dot - entity) >= sizeof(domain)) {
        ESP_LOGW(TAG, "entity_id raro: %s", entity);
        return;
    }
    memcpy(domain, entity, dot - entity);
    const char *service = state ? "turn_on" : "turn_off";
    char body[160];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity);
    esp_err_t r = ha_service_call(domain, service, body);
    ESP_LOGI(TAG, "  -> %s.%s %s (r=%d)", domain, service, entity, r);
}

/* on_luz_changed: la version weak de luces_screen.c solo loguea.
 * Esta version (fuerte) ademas manda el comando real a HA. */
void on_luz_changed(int idx, bool state)
{
    if (idx < 0 || idx >= 6) return;
    ESP_LOGI(TAG, "Luz %d -> %s", idx, state ? "ON" : "OFF");
    luz_enviar(LUZ_ENTITY[idx], state);
    if (LUZ_ENTITY_EXTRA[idx]) luz_enviar(LUZ_ENTITY_EXTRA[idx], state);
}

/* Devuelve true si el WiFi STA tiene IP asignada (conectado de verdad) */
static bool wifi_esta_conectado(void)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) return false;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(sta, &ip) != ESP_OK) return false;
    return ip.ip.addr != 0;   /* IP != 0.0.0.0 */
}

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
            app_nav_set_wifi(wifi_esta_conectado());
            app_nav_set_ha(ha_client_is_connected());
            app_nav_set_datetime(date_buf, time_buf);
            lvgl_port_unlock();
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "SM Domotica - ESP32-P4 arrancando");
    nvs_flash_init();

    /* ====== MODO OTA C6: poner en 1 para actualizar el C6, luego volver a 0 ====== */
#define OTA_C6_MODE 0
#if OTA_C6_MODE
    wifi_provision_ota_c6("embedded");
    return;
#endif

    /* ====== CHECK VERSION C6: poner en 1 para leer la version del C6 ====== */
#define CHECK_C6_VERSION 0
#if CHECK_C6_VERSION
    wifi_provision_check_c6_version();
    /* sigue con la app normal despues de mostrar la version */
#endif
    /* ============================================================================ */
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
    /* WiFi: si hay credenciales conecta (STA); si no, muestra pantalla de config tactil */
    if (wifi_provision_has_credentials()) {
        ESP_LOGI(TAG, "Credenciales guardadas, conectando (STA)...");
        if (wifi_provision_connect()) {
            ESP_LOGI(TAG, "WiFi CONECTADO");
            /* Arrancar cliente Home Assistant */
            ha_client_set_connected_cb(on_ha_connected);
            ha_client_set_state_cb(on_ha_state);
            if (ha_client_init(HA_HOST, HA_PORT, HA_TOKEN) == ESP_OK) {
                ha_client_start();
                ESP_LOGI(TAG, "Cliente HA iniciado, conectando a %s:%d", HA_HOST, HA_PORT);
            } else {
                ESP_LOGE(TAG, "Fallo ha_client_init");
            }
        }
        else {
            ESP_LOGE(TAG, "Fallo conexion - mostrando config");
            if (lvgl_port_lock(0)) { wifi_config_screen_show(); lvgl_port_unlock(); }
        }
    } else {
        ESP_LOGW(TAG, "Sin credenciales - mostrando pantalla de config WiFi");
        if (lvgl_port_lock(0)) { wifi_config_screen_show(); lvgl_port_unlock(); }
    }
    xTaskCreate(status_task, "status", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
}
