#pragma once
/**
 * @file event_bus.h
 * @brief Lightweight publish-subscribe event bus using FreeRTOS queues.
 *
 * All inter-task communication goes through this bus.
 * Producers call event_bus_post().
 * Consumers call event_bus_subscribe() and receive events on a dedicated queue.
 *
 * Event IDs 0x0000–0x00FF : System
 * Event IDs 0x0100–0x01FF : Spotify / playback
 * Event IDs 0x0200–0x02FF : Audio pipeline
 * Event IDs 0x0300–0x03FF : Bluetooth
 * Event IDs 0x0400–0x04FF : UI
 */

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Event IDs ──────────────────────────────────────────────────────────────*/
typedef enum {
    /* System */
    EVT_SYS_BOOT_DONE        = 0x0001,
    EVT_SYS_WIFI_CONNECTED   = 0x0002,
    EVT_SYS_WIFI_DISCONNECTED= 0x0003,
    EVT_SYS_OTA_START        = 0x0010,
    EVT_SYS_OTA_DONE         = 0x0011,

    /* Spotify / playback */
    EVT_SPOTIFY_CONNECTED    = 0x0100,
    EVT_SPOTIFY_DISCONNECTED = 0x0101,
    EVT_TRACK_CHANGED        = 0x0102,
    EVT_PLAYBACK_STATE       = 0x0103,   /* play/pause/stop */
    EVT_PROGRESS_UPDATE      = 0x0104,
    EVT_VOLUME_CHANGED       = 0x0105,
    EVT_ALBUM_ART_READY      = 0x0106,
    EVT_PALETTE_READY        = 0x0107,

    /* Audio */
    EVT_AUDIO_MODE_CHANGED   = 0x0200,
    EVT_FFT_DATA_READY       = 0x0201,

    /* Bluetooth */
    EVT_BT_SCAN_RESULT       = 0x0300,
    EVT_BT_CONNECTED         = 0x0301,
    EVT_BT_DISCONNECTED      = 0x0302,
    EVT_BT_PAIR_REQUEST      = 0x0303,

    /* UI */
    EVT_UI_SCREEN_CHANGE     = 0x0400,
    EVT_UI_TOUCH_IDLE        = 0x0401,
    EVT_UI_TOUCH_ACTIVE      = 0x0402,
} event_id_t;

/* ── Event payload ──────────────────────────────────────────────────────────*/
typedef struct {
    event_id_t  id;
    uint32_t    timestamp_ms;
    union {
        uint32_t    u32;
        int32_t     i32;
        float       f32;
        void       *ptr;
        uint8_t     bytes[8];
    } data;
} app_event_t;

/* ── Subscriber handle ──────────────────────────────────────────────────────*/
typedef QueueHandle_t event_sub_t;

/**
 * @brief Initialise the event bus (call once from app_main, before any task).
 */
void event_bus_init(void);

/**
 * @brief Subscribe to all events.  Returns a queue to receive from.
 * @param queue_depth  How many unread events to buffer before dropping oldest.
 */
event_sub_t event_bus_subscribe(uint8_t queue_depth);

/**
 * @brief Post an event to all subscribers.
 * @param evt    Event to broadcast.  Copied by value.
 * @param from_isr  true if called from an ISR context.
 */
void event_bus_post(const app_event_t *evt, bool from_isr);

/**
 * @brief Convenience: post event with a u32 payload.
 */
static inline void event_post_u32(event_id_t id, uint32_t val)
{
    app_event_t e = { .id = id, .data.u32 = val };
    e.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    event_bus_post(&e, false);
}

/**
 * @brief Convenience: post event with a pointer payload.
 */
static inline void event_post_ptr(event_id_t id, void *ptr)
{
    app_event_t e = { .id = id, .data.ptr = ptr };
    e.timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    event_bus_post(&e, false);
}

#ifdef __cplusplus
}
#endif
