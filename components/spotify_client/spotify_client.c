/**
 * @file spotify_client.c
 * @brief Control Spotify via Home Assistant REST API
 * Usa el token de HA para controlar media_player.spotify_premiun
 */

#include "spotify_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "spotify";

#define HA_ENTITY      "media_player.spotify_premiun"
#define POLL_INTERVAL  3

static char s_ha_host[64]   = {0};
static char s_ha_token[512] = {0};
static uint16_t s_ha_port   = 8123;

static spotify_playback_cb_t  s_playback_cb  = NULL;
static spotify_playlists_cb_t s_playlists_cb = NULL;
static spotify_tracks_cb_t    s_tracks_cb    = NULL;

/* ── HTTP buffer ── */
typedef struct { char *buf; int len; int cap; } http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *b = (http_buf_t *)evt->user_data;
    if (!b) return ESP_OK;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        if (b->len + evt->data_len < b->cap - 1) {
            memcpy(b->buf + b->len, evt->data, evt->data_len);
            b->len += evt->data_len;
            b->buf[b->len] = 0;
        }
    }
    return ESP_OK;
}

static char *ha_get(const char *path)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", s_ha_host, s_ha_port, path);

    char *out = calloc(1, 8192);
    if (!out) return NULL;
    http_buf_t b = {out, 0, 8192};

    char auth[600];
    snprintf(auth, sizeof(auth), "Bearer %s", s_ha_token);

    esp_http_client_config_t cfg = {
        .url           = url,
        .timeout_ms    = 8000,
        .event_handler = http_event_handler,
        .user_data     = &b,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { free(out); return NULL; }
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status < 200 || status >= 300) {
        free(out); return NULL;
    }
    return out;
}

static esp_err_t ha_post(const char *path, const char *body)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", s_ha_host, s_ha_port, path);

    char auth[600];
    snprintf(auth, sizeof(auth), "Bearer %s", s_ha_token);

    char *out = calloc(1, 512);
    if (!out) return ESP_FAIL;
    http_buf_t b = {out, 0, 512};

    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = HTTP_METHOD_POST,
        .timeout_ms    = 8000,
        .event_handler = http_event_handler,
        .user_data     = &b,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { free(out); return ESP_FAIL; }
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    if (body) esp_http_client_set_post_field(c, body, strlen(body));
    esp_http_client_perform(c);
    esp_http_client_cleanup(c);
    free(out);
    return ESP_OK;
}

/* ── Poll playback state from HA ── */
static void poll_playback(void)
{
    char *resp = ha_get("/api/states/" HA_ENTITY);
    if (!resp) return;

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) return;

    spotify_playback_t pb = {0};

    cJSON *state = cJSON_GetObjectItem(json, "state");
    if (state && cJSON_IsString(state))
        pb.is_playing = (strcmp(state->valuestring, "playing") == 0);

    cJSON *attrs = cJSON_GetObjectItem(json, "attributes");
    if (attrs) {
        cJSON *title = cJSON_GetObjectItem(attrs, "media_title");
        if (title && cJSON_IsString(title))
            strncpy(pb.track.name, title->valuestring, sizeof(pb.track.name)-1);

        cJSON *artist = cJSON_GetObjectItem(attrs, "media_artist");
        if (artist && cJSON_IsString(artist))
            strncpy(pb.track.artist, artist->valuestring, sizeof(pb.track.artist)-1);

        cJSON *album = cJSON_GetObjectItem(attrs, "media_album_name");
        if (album && cJSON_IsString(album))
            strncpy(pb.track.album, album->valuestring, sizeof(pb.track.album)-1);

        cJSON *dur = cJSON_GetObjectItem(attrs, "media_duration");
        if (dur && cJSON_IsNumber(dur))
            pb.track.duration_ms = (int)(dur->valuedouble * 1000);

        cJSON *pos = cJSON_GetObjectItem(attrs, "media_position");
        if (pos && cJSON_IsNumber(pos))
            pb.progress_ms = (int)(pos->valuedouble * 1000);

        cJSON *img = cJSON_GetObjectItem(attrs, "entity_picture");
        if (img && cJSON_IsString(img)) {
            snprintf(pb.track.image_url, sizeof(pb.track.image_url),
                     "http://%s:%d%s", s_ha_host, s_ha_port, img->valuestring);
        }
    }

    cJSON_Delete(json);
    if (s_playback_cb) s_playback_cb(&pb);
}

/* ── Playback control via HA services ── */
esp_err_t spotify_play_pause(void)
{
    if (strlen(s_ha_host) == 0) return ESP_ERR_INVALID_STATE;
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", HA_ENTITY);
    return ha_post("/api/services/media_player/media_play_pause", body);
}

esp_err_t spotify_next(void)
{
    if (strlen(s_ha_host) == 0) return ESP_ERR_INVALID_STATE;
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", HA_ENTITY);
    return ha_post("/api/services/media_player/media_next_track", body);
}

esp_err_t spotify_prev(void)
{
    if (strlen(s_ha_host) == 0) return ESP_ERR_INVALID_STATE;
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", HA_ENTITY);
    return ha_post("/api/services/media_player/media_previous_track", body);
}

esp_err_t spotify_seek(int position_ms)
{
    char body[128];
    snprintf(body, sizeof(body),
             "{\"entity_id\":\"%s\",\"seek_position\":%.1f}",
             HA_ENTITY, position_ms / 1000.0f);
    return ha_post("/api/services/media_player/media_seek", body);
}

esp_err_t spotify_set_volume(int vol)
{
    char body[128];
    snprintf(body, sizeof(body),
             "{\"entity_id\":\"%s\",\"volume_level\":%.2f}",
             HA_ENTITY, vol / 100.0f);
    return ha_post("/api/services/media_player/volume_set", body);
}

esp_err_t spotify_play_uri(const char *uri)
{
    char body[256];
    snprintf(body, sizeof(body),
             "{\"entity_id\":\"%s\",\"media_content_id\":\"%s\",\"media_content_type\":\"music\"}",
             HA_ENTITY, uri);
    return ha_post("/api/services/media_player/play_media", body);
}

esp_err_t spotify_get_playlists(void) { return ESP_OK; }
esp_err_t spotify_get_playlist_tracks(const char *id) { return ESP_OK; }
esp_err_t spotify_search(const char *q) { return ESP_OK; }
esp_err_t spotify_get_current_playback(void) { poll_playback(); return ESP_OK; }

esp_err_t spotify_exchange_code(const char *code,
                                 char *at, size_t al,
                                 char *rt, size_t rl)
{ return ESP_FAIL; }

/* ── Background task ── */
static void spotify_task(void *arg)
{
    int tick = 0;
    while (1) {
        if (tick % POLL_INTERVAL == 0) poll_playback();
        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ── Init ── */
esp_err_t spotify_client_init(const char *ha_host, uint16_t ha_port,
                               const char *ha_token, const char *device_id)
{
    strncpy(s_ha_host,  ha_host,  sizeof(s_ha_host)-1);
    strncpy(s_ha_token, ha_token, sizeof(s_ha_token)-1);
    s_ha_port = ha_port;
    ESP_LOGI(TAG, "Spotify via HA REST API en %s:%d", ha_host, ha_port);
    return ESP_OK;
}

esp_err_t spotify_client_start(void)
{
    BaseType_t r = xTaskCreatePinnedToCore(
        spotify_task, "spotify", 6144, NULL, 4, NULL, 1);
    return (r == pdPASS) ? ESP_OK : ESP_FAIL;
}

void spotify_set_playback_cb(spotify_playback_cb_t cb)   { s_playback_cb  = cb; }
void spotify_set_playlists_cb(spotify_playlists_cb_t cb) { s_playlists_cb = cb; }
void spotify_set_tracks_cb(spotify_tracks_cb_t cb)       { s_tracks_cb    = cb; }
