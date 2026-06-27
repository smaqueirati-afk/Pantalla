/**
 * @file app_state.c
 * @brief Central app state — thread-safe access + NVS persistence.
 */

#include "state/app_state.h"
#include "state/event_bus.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "app_state";
#define NVS_NS "player_cfg"

static app_state_t    s_state;
static SemaphoreHandle_t s_mutex = NULL;

/* ── NVS helpers ────────────────────────────────────────────────────────────*/
static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "No saved config — using defaults");
        return;
    }
    uint8_t v = s_state.volume_pct;
    nvs_get_u8(h, "volume", &v); s_state.volume_pct = v;
    v = s_state.volume_bt_pct;
    nvs_get_u8(h, "volume_bt", &v); s_state.volume_bt_pct = v;
    uint8_t mode = (uint8_t)s_state.audio_mode;
    nvs_get_u8(h, "audio_mode", &mode);
    s_state.audio_mode = (audio_mode_t)mode;
    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded — vol=%d mode=%d", s_state.volume_pct, s_state.audio_mode);
}

static void nvs_save_volume(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "volume", s_state.volume_pct);
    nvs_set_u8(h, "volume_bt", s_state.volume_bt_pct);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_audio_mode(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "audio_mode", (uint8_t)s_state.audio_mode);
    nvs_commit(h);
    nvs_close(h);
}

/* ── Init ───────────────────────────────────────────────────────────────────*/
void app_state_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    memset(&s_state, 0, sizeof(s_state));
    /* Defaults */
    s_state.volume_pct    = 70;
    s_state.volume_bt_pct = 70;
    s_state.audio_mode    = AUDIO_MODE_INTERNAL;
    s_state.playback      = PLAYBACK_STOPPED;

    nvs_load();
    ESP_LOGI(TAG, "App state initialised");
}

const app_state_t *app_state_get(void) { return &s_state; }

/* ── Setters ────────────────────────────────────────────────────────────────*/
#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

void app_state_set_playback(playback_state_t state)
{
    LOCK();
    s_state.playback = state;
    UNLOCK();
    event_post_u32(EVT_PLAYBACK_STATE, (uint32_t)state);
}

void app_state_set_track(const track_info_t *track)
{
    LOCK();
    memcpy(&s_state.track, track, sizeof(track_info_t));
    s_state.album_art_ready = false;
    s_state.palette_ready   = false;
    UNLOCK();
    event_post_ptr(EVT_TRACK_CHANGED, (void *)&s_state.track);
}

void app_state_set_volume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    LOCK();
    s_state.volume_pct = pct;
    UNLOCK();
    nvs_save_volume();
    event_post_u32(EVT_VOLUME_CHANGED, pct);
}

void app_state_set_volume_bt(uint8_t pct)
{
    if (pct > 100) pct = 100;
    LOCK();
    s_state.volume_bt_pct = pct;
    UNLOCK();
    nvs_save_volume();
}

void app_state_set_audio_mode(audio_mode_t mode)
{
    LOCK();
    s_state.audio_mode = mode;
    UNLOCK();
    nvs_save_audio_mode();
    event_post_u32(EVT_AUDIO_MODE_CHANGED, (uint32_t)mode);
}

void app_state_set_album_art(void *buf, uint32_t w, uint32_t h)
{
    LOCK();
    s_state.album_art_buf   = buf;
    s_state.album_art_w     = w;
    s_state.album_art_h     = h;
    s_state.album_art_ready = true;
    UNLOCK();
    event_post_ptr(EVT_ALBUM_ART_READY, buf);
}

void app_state_set_palette(const color_palette_t *palette)
{
    LOCK();
    memcpy(&s_state.palette, palette, sizeof(color_palette_t));
    s_state.palette_ready = true;
    UNLOCK();
    event_post_ptr(EVT_PALETTE_READY, (void *)&s_state.palette);
}

void app_state_set_bt_connected(const bt_device_t *dev)
{
    LOCK();
    memcpy(&s_state.bt_connected_dev, dev, sizeof(bt_device_t));
    s_state.bt_connected = true;
    UNLOCK();
    event_post_ptr(EVT_BT_CONNECTED, (void *)&s_state.bt_connected_dev);
}

void app_state_set_bt_disconnected(void)
{
    LOCK();
    s_state.bt_connected = false;
    UNLOCK();
    event_post_u32(EVT_BT_DISCONNECTED, 0);
}

void app_state_set_wifi(bool connected, const char *ssid)
{
    LOCK();
    s_state.wifi_connected = connected;
    if (ssid) strncpy(s_state.wifi_ssid, ssid, sizeof(s_state.wifi_ssid) - 1);
    UNLOCK();
    event_post_u32(connected ? EVT_SYS_WIFI_CONNECTED : EVT_SYS_WIFI_DISCONNECTED, 0);
}

void app_state_set_progress(uint32_t position_ms)
{
    LOCK();
    s_state.track.position_ms = position_ms;
    UNLOCK();
    event_post_u32(EVT_PROGRESS_UPDATE, position_ms);
}
