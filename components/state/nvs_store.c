#include "state/nvs_store.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include <string.h>

static const char *TAG = "nvs_store";
#define NVS_NS_WIFI "wifi_cfg"

esp_err_t nvs_store_wifi_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h), TAG, "nvs open");
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_load_wifi_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS_WIFI, NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;
    nvs_get_str(h, "ssid", ssid, &ssid_len);
    nvs_get_str(h, "pass", pass, &pass_len);
    nvs_close(h);
    return ESP_OK;
}
