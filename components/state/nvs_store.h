#pragma once
/**
 * @file nvs_store.h
 * @brief Extended NVS helpers — WiFi credentials, BT device list, etc.
 *        Used by Fase 2+ modules.
 */
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_store_wifi_creds(const char *ssid, const char *pass);
esp_err_t nvs_load_wifi_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

#ifdef __cplusplus
}
#endif
