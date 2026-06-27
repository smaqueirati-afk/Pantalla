/**
 * @file audio_feedback.c
 * @brief Audio feedback via BSP audio init + I2S write directo
 */

#include "audio_feedback.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "audio_fb";

#define SAMPLE_RATE     22050
#define PA_CTRL_GPIO    53

static esp_codec_dev_handle_t s_spk_dev  = NULL;
static bool                   s_initialized = false;

typedef struct { int freq; int duration_ms; float vol; } beep_t;
static QueueHandle_t s_beep_queue = NULL;

static void play_tone(int freq_hz, int duration_ms, float amplitude)
{
    if (!s_spk_dev || !s_initialized) return;
    int samples = (SAMPLE_RATE * duration_ms) / 1000;
    int16_t *buf = malloc(samples * 2 * sizeof(int16_t));
    if (!buf) return;
    float step = 2.0f * M_PI * freq_hz / SAMPLE_RATE;
    int fade = SAMPLE_RATE * 8 / 1000;
    for (int i = 0; i < samples; i++) {
        float env = 1.0f;
        if (i < fade) env = (float)i / fade;
        else if (i > samples - fade) env = (float)(samples - i) / fade;
        int16_t s = (int16_t)(sinf(step * i) * amplitude * env * 32767.0f);
        buf[i*2] = s; buf[i*2+1] = s;
    }
    /* PA ON */
    gpio_set_level(PA_CTRL_GPIO, 1);
    int ret = esp_codec_dev_write(s_spk_dev, buf, samples * 2 * sizeof(int16_t));
    if (ret < 0) ESP_LOGW(TAG, "write err: %d", ret);
    /* PA OFF after done */
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PA_CTRL_GPIO, 0);
    free(buf);
}

static void beep_task(void *arg)
{
    beep_t b;
    while (1) {
        if (xQueueReceive(s_beep_queue, &b, portMAX_DELAY)) {
            play_tone(b.freq, b.duration_ms, b.vol);
        }
    }
}

static void queue_beep(int freq, int ms, float vol)
{
    if (!s_initialized || !s_beep_queue) return;
    beep_t b = {freq, ms, vol};
    xQueueSend(s_beep_queue, &b, 0);
}

void audio_beep_short(void)  { queue_beep(1200, 60,  0.7f); }
void audio_beep_on(void)     { queue_beep(880,  120, 0.8f); }
void audio_beep_off(void)    { queue_beep(440,  120, 0.7f); }
void audio_beep_porton(void) {
    queue_beep(880,  80, 0.9f);
    vTaskDelay(pdMS_TO_TICKS(120));
    queue_beep(1100, 80, 0.9f);
}

esp_err_t audio_feedback_init(void)
{
    /* PA GPIO */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << PA_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(PA_CTRL_GPIO, 0);

    /* BSP speaker init - maneja ES8311 + I2S + PA */
    s_spk_dev = bsp_audio_codec_speaker_init();
    if (!s_spk_dev) {
        ESP_LOGE(TAG, "bsp_audio_codec_speaker_init failed");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = SAMPLE_RATE,
        .channel         = 2,
        .bits_per_sample = 16,
    };
    int ret = esp_codec_dev_open(s_spk_dev, &fs);
    ESP_LOGI(TAG, "codec open ret: %d", ret);
    
    ret = esp_codec_dev_set_out_vol(s_spk_dev, 100);
    ESP_LOGI(TAG, "codec vol ret: %d", ret);

    s_beep_queue = xQueueCreate(4, sizeof(beep_t));
    xTaskCreatePinnedToCore(beep_task, "beep", 8192, NULL, 5, NULL, 1);
    s_initialized = true;
    ESP_LOGI(TAG, "Audio feedback OK - vol=100");
    return ESP_OK;
}

esp_err_t audio_feedback_set_rate(int rate)
{
    if (!s_spk_dev) return ESP_FAIL;
    esp_codec_dev_close(s_spk_dev);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = rate,
        .channel         = 2,
        .bits_per_sample = 16,
    };
    int ret = esp_codec_dev_open(s_spk_dev, &fs);
    ESP_LOGI(TAG, "codec reopen %dHz ret=%d", rate, ret);
    gpio_set_level(PA_CTRL_GPIO, 1);  // PA ON para streaming
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_codec_dev_handle_t audio_feedback_get_codec(void)
{
    return s_spk_dev;
}

