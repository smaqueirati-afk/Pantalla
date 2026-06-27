/**
 * @file radio_player.c
 * @brief HTTP MP3 radio streaming con reconexión automática
 */

#include "radio_player.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mp3dec.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "radio";

#define RADIO_BUF_SIZE   (16 * 1024)  /* Buffer grande para streaming estable */
#define PCM_BUF_FRAMES   (1152 * 2)
#define STOP_BIT         BIT0
#define MAX_RETRIES      5

static esp_codec_dev_handle_t s_spk     = NULL;
static TaskHandle_t           s_task    = NULL;
static EventGroupHandle_t     s_eg      = NULL;
static bool                   s_playing = false;
static char                   s_url[256] = {0};
static int                    s_vol     = 85;

static esp_http_client_handle_t connect_stream(void)
{
    esp_http_client_config_t cfg = {
        .url              = s_url,
        .buffer_size      = RADIO_BUF_SIZE,
        .timeout_ms       = 20000,
        .keep_alive_enable = true,
        .keep_alive_idle   = 5,
        .keep_alive_interval = 5,
        .keep_alive_count  = 3,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return NULL;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return NULL;
    }
    esp_http_client_fetch_headers(client);
    return client;
}

static void radio_task(void *arg)
{
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) { ESP_LOGE(TAG, "MP3 decoder fail"); goto done; }

    uint8_t *in_buf  = heap_caps_malloc(RADIO_BUF_SIZE, MALLOC_CAP_SPIRAM);
    int16_t *pcm_buf = heap_caps_malloc(PCM_BUF_FRAMES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!in_buf || !pcm_buf) { ESP_LOGE(TAG, "OOM"); MP3FreeDecoder(decoder); goto done; }

    esp_codec_dev_set_out_vol(s_spk, s_vol);

    int retry = 0;
    while (!(xEventGroupGetBits(s_eg) & STOP_BIT) && retry < MAX_RETRIES) {
        ESP_LOGI(TAG, "Conectando (intento %d): %s", retry + 1, s_url);
        esp_http_client_handle_t client = connect_stream();
        if (!client) {
            ESP_LOGW(TAG, "Conexion fallida, reintentando en 3s...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            retry++;
            continue;
        }

        retry = 0; /* reset retries on successful connection */
        int in_left = 0;
        uint8_t *in_ptr = in_buf;
        int consecutive_errors = 0;
        ESP_LOGI(TAG, "Reproduciendo...");

        while (!(xEventGroupGetBits(s_eg) & STOP_BIT)) {
            /* Rellenar buffer */
            if (in_left < RADIO_BUF_SIZE / 2) {
                memmove(in_buf, in_ptr, in_left);
                in_ptr = in_buf;
                int rd = esp_http_client_read(client,
                            (char*)(in_buf + in_left),
                            RADIO_BUF_SIZE - in_left);
                if (rd < 0) {
                    ESP_LOGW(TAG, "Error de red, reconectando...");
                    consecutive_errors++;
                    if (consecutive_errors > 3) break;
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                if (rd == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }
                in_left += rd;
                consecutive_errors = 0;
            }

            /* Buscar sync word */
            int offset = MP3FindSyncWord(in_ptr, in_left);
            if (offset < 0) {
                in_left = 0;
                in_ptr = in_buf;
                continue;
            }
            in_ptr  += offset;
            in_left -= offset;

            /* Decodificar frame */
            MP3FrameInfo info;
            int err = MP3Decode(decoder, &in_ptr, &in_left, pcm_buf, 0);
            if (err != ERR_MP3_NONE) continue;

            MP3GetLastFrameInfo(decoder, &info);
            esp_codec_dev_write(s_spk, pcm_buf, info.outputSamps * sizeof(int16_t));
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (!(xEventGroupGetBits(s_eg) & STOP_BIT)) {
            ESP_LOGW(TAG, "Stream cortado, reconectando en 2s...");
            retry++;
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    free(in_buf);
    free(pcm_buf);
    MP3FreeDecoder(decoder);
done:
    s_playing = false;
    s_task    = NULL;
    ESP_LOGI(TAG, "Radio detenida");
    vTaskDelete(NULL);
}

esp_err_t radio_player_init(esp_codec_dev_handle_t spk_dev)
{
    ESP_LOGI(TAG, "radio_player_init spk_dev=%p", (void*)spk_dev);
    if (!spk_dev) return ESP_ERR_INVALID_ARG;
    s_spk = spk_dev;
    if (!s_eg) s_eg = xEventGroupCreate();
    if (!s_eg) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Radio player OK");
    return ESP_OK;
}

esp_err_t radio_player_play(const char *url)
{
    if (!s_eg) { ESP_LOGE(TAG, "No init"); return ESP_ERR_INVALID_STATE; }
    if (!url || strlen(url) < 5) return ESP_ERR_INVALID_ARG;
    if (s_playing) radio_player_stop();
    strncpy(s_url, url, sizeof(s_url) - 1);
    xEventGroupClearBits(s_eg, STOP_BIT);
    s_playing = true;
    xTaskCreatePinnedToCore(radio_task, "radio", 12288, NULL, 4, &s_task, 1);
    return ESP_OK;
}

esp_err_t radio_player_stop(void)
{
    if (!s_eg || !s_playing) return ESP_OK;
    xEventGroupSetBits(s_eg, STOP_BIT);
    int t = 60;
    while (s_playing && t-- > 0) vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

bool radio_player_is_playing(void) { return s_playing; }

void radio_player_preinit(void)
{
    if (!s_eg) s_eg = xEventGroupCreate();
}

void radio_player_set_vol(int vol)
{
    s_vol = (vol < 0) ? 0 : (vol > 100) ? 100 : vol;
    if (s_spk) esp_codec_dev_set_out_vol(s_spk, s_vol);
    ESP_LOGI(TAG, "Vol: %d", s_vol);
}
