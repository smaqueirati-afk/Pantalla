#include "wifi_manager.h"
// #include "c6_ota.h" // desactivado temporal
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
// #include "esp_hosted.h" // desactivado temporal
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_mgr";
static bool s_connected = false;
static char s_ha_url[128] = {0};
static char s_ha_token[256] = {0};

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Cliente conectado al AP!");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "STA disco reason=%d", d->reason);
        s_connected = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "=== IP: " IPSTR " ===", IP2STR(&e->ip_info.ip));
        s_connected = true;
    }
}

void wifi_manager_start(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    nvs_flash_init();

    /* CRITICO: inicializar esp_hosted ANTES de cualquier WiFi */
    int r = 0; /* esp_hosted_init() desactivado */
    ESP_LOGI(TAG, "esp_hosted_init: %d", r);
    r = -1; /* esp_hosted_connect_to_slave() desactivado */
    ESP_LOGI(TAG, "esp_hosted_connect_to_slave: %d", r);
    if (r != 0) {
        ESP_LOGE(TAG, "SDIO no funciona, abortando WiFi setup");
        return;
    }

    /* c6_ota_check_and_update() desactivado */

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    uint8_t ap_mac[6] = {0x80, 0xF1, 0xB2, 0xD3, 0x2A, 0x32};
    esp_wifi_set_mac(WIFI_IF_AP, ap_mac);

    wifi_config_t ap_cfg = {.ap = {
        .ssid = "SM-Test", .password = "12345678",
        .ssid_len = 7, .channel = 6,
        .authmode = WIFI_AUTH_WPA2_PSK, .max_connection = 2
    }};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Forzar full power - no power save, max TX */
    esp_wifi_set_max_tx_power(78);  /* 19.5 dBm */
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    ESP_LOGI(TAG, "=== AP: SM-Test / 12345678 ===");
}

bool wifi_manager_is_connected(void)           { return s_connected; }
void wifi_manager_get_ha_url(char *b, int l)   { strlcpy(b, s_ha_url, l); }
void wifi_manager_get_ha_token(char *b, int l) { strlcpy(b, s_ha_token, l); }
