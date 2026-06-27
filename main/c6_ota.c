#include "c6_ota.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_hosted.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "c6_ota";
#define CHUNK_SIZE 4096

esp_err_t c6_ota_check_and_update(void)
{
    ESP_LOGI(TAG, "Iniciando OTA del C6...");
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 0x40, "slave_fw");
    if (!part) {
        ESP_LOGE(TAG, "Particion slave_fw no encontrada");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "slave_fw: addr=0x%x size=%d KB",
             (unsigned)part->address, (int)(part->size/1024));
    uint8_t magic = 0;
    esp_partition_read(part, 0, &magic, 1);
    if (magic != 0xE9) {
        ESP_LOGE(TAG, "Magic invalido: 0x%02x", magic);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t e = esp_hosted_slave_ota_begin();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %d", e);
        return e;
    }
    uint8_t *buf = malloc(CHUNK_SIZE);
    if (!buf) return ESP_ERR_NO_MEM;
    size_t offset = 0;
    while (offset < part->size) {
        size_t to_read = part->size - offset;
        if (to_read > CHUNK_SIZE) to_read = CHUNK_SIZE;
        uint8_t all_ff = 1;
        esp_partition_read(part, offset, buf, to_read);
        for (size_t i = 0; i < to_read; i++) {
            if (buf[i] != 0xFF) { all_ff = 0; break; }
        }
        if (all_ff && offset > 0x10000) {
            ESP_LOGI(TAG, "Fin del firmware en offset %d", (int)offset);
            break;
        }
        e = esp_hosted_slave_ota_write(buf, to_read);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "ota_write offset=%d: %d", (int)offset, e);
            free(buf);
            return e;
        }
        offset += to_read;
        if (offset % (CHUNK_SIZE * 8) == 0) {
            ESP_LOGI(TAG, "OTA: %d KB...", (int)(offset/1024));
        }
    }
    free(buf);
    e = esp_hosted_slave_ota_end();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %d", e);
        return e;
    }
    ESP_LOGI(TAG, "OTA OK — reiniciando...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}



