/**
 * @file event_bus.c
 * @brief Lightweight publish-subscribe event bus.
 *
 * Implementation: a linked list of subscriber queues.
 * post() iterates the list and xQueueSend() to each one.
 * If a subscriber's queue is full the event is dropped for that subscriber
 * (non-blocking) — audio_task and ui_task must size their queues accordingly.
 */

#include "state/event_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "event_bus";

#define MAX_SUBSCRIBERS 16

static QueueHandle_t  s_subs[MAX_SUBSCRIBERS];
static uint8_t        s_sub_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

void event_bus_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);
    s_sub_count = 0;
    ESP_LOGI(TAG, "Event bus ready (max %d subscribers)", MAX_SUBSCRIBERS);
}

event_sub_t event_bus_subscribe(uint8_t queue_depth)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_sub_count >= MAX_SUBSCRIBERS) {
        ESP_LOGE(TAG, "Subscriber limit reached!");
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    QueueHandle_t q = xQueueCreate(queue_depth, sizeof(app_event_t));
    configASSERT(q);
    s_subs[s_sub_count++] = q;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "New subscriber — total %d", s_sub_count);
    return q;
}

void event_bus_post(const app_event_t *evt, bool from_isr)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint8_t i = 0; i < s_sub_count; i++) {
        if (from_isr) {
            BaseType_t woken = pdFALSE;
            xQueueSendFromISR(s_subs[i], evt, &woken);
        } else {
            if (xQueueSend(s_subs[i], evt, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Subscriber %d queue full — event 0x%04X dropped", i, evt->id);
            }
        }
    }
    xSemaphoreGive(s_mutex);
}
