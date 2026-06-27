/**
 * @file ma_client.c
 * @brief Music Assistant WebSocket client
 * ws://192.168.1.100:8095/ws
 * Token: afb360b7e4684f188837987a0f516c83
 */

#include "ma_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ma_client";

static esp_websocket_client_handle_t s_client = NULL;
static ma_player_cb_t    s_player_cb    = NULL;
static ma_connected_cb_t s_connected_cb = NULL;
static bool s_connected    = false;
static bool s_authenticated = false;
static int  s_msg_id       = 1;
static char s_host[64]     = {0};
static char s_token[128]   = {0};
static uint16_t s_port     = 8095;

/* Spotify player ID — actualizado cuando llega del server */
static char s_player_id[64] = "spotify_premiun";

/* ── Send JSON ── */
static void ma_send(cJSON *json)
{
    if (!s_client || !s_connected) return;
    char *str = cJSON_PrintUnformatted(json);
    if (!str) return;
    ESP_LOGD(TAG, "TX: %.100s", str);
    esp_websocket_client_send_text(s_client, str, strlen(str), pdMS_TO_TICKS(3000));
    free(str);
}

/* ── Auth ── */
static void ma_authenticate(void)
{
    /* MA schema 29 local: token en URL es suficiente, ir directo a players/all */
    cJSON *get = cJSON_CreateObject();
    cJSON_AddStringToObject(get, "message_id", "players-1");
    cJSON_AddStringToObject(get, "command", "players/all");
    cJSON *args = cJSON_CreateObject();
    cJSON_AddItemToObject(get, "args", args);
    ma_send(get);
    cJSON_Delete(get);
    s_authenticated = true;
    ESP_LOGI(TAG, "MA: players/all enviado directamente");
    if (s_connected_cb) s_connected_cb(true);
}

static void ma_get_players(void)
{
    /* Ya implementado en ma_authenticate */
}

/* ── Parse player update ── */
static void parse_player(cJSON *data)
{
    if (!data || !s_player_cb) return;

    ma_player_t p = {0};
    cJSON *pid = cJSON_GetObjectItem(data, "player_id");
    if (pid && cJSON_IsString(pid))
        strncpy(p.player_id, pid->valuestring, sizeof(p.player_id)-1);

    cJSON *name = cJSON_GetObjectItem(data, "display_name");
    if (name && cJSON_IsString(name))
        strncpy(p.name, name->valuestring, sizeof(p.name)-1);

    cJSON *state = cJSON_GetObjectItem(data, "state");
    if (state && cJSON_IsString(state))
        p.playing = (strcmp(state->valuestring, "playing") == 0);

    cJSON *vol = cJSON_GetObjectItem(data, "volume_level");
    if (vol && cJSON_IsNumber(vol))
        p.volume = (float)vol->valuedouble;

    cJSON *elapsed = cJSON_GetObjectItem(data, "elapsed_time");
    if (elapsed && cJSON_IsNumber(elapsed))
        p.elapsed_time = (int)elapsed->valuedouble;

    /* Current item */
    cJSON *item = cJSON_GetObjectItem(data, "current_item");
    if (item) {
        cJSON *t = cJSON_GetObjectItem(item, "name");
        if (t && cJSON_IsString(t))
            strncpy(p.current_item.title, t->valuestring, sizeof(p.current_item.title)-1);

        cJSON *dur = cJSON_GetObjectItem(item, "duration");
        if (dur && cJSON_IsNumber(dur))
            p.current_item.duration = (int)dur->valuedouble;

        /* Artists */
        cJSON *artists = cJSON_GetObjectItem(item, "artists");
        if (artists && cJSON_IsArray(artists) && cJSON_GetArraySize(artists) > 0) {
            cJSON *a = cJSON_GetArrayItem(artists, 0);
            cJSON *aname = cJSON_GetObjectItem(a, "name");
            if (aname && cJSON_IsString(aname))
                strncpy(p.current_item.artist, aname->valuestring, sizeof(p.current_item.artist)-1);
        }

        /* Image */
        cJSON *img = cJSON_GetObjectItem(item, "image");
        if (img) {
            cJSON *ipath = cJSON_GetObjectItem(img, "path");
            if (ipath && cJSON_IsString(ipath)) {
                snprintf(p.current_item.image_url, sizeof(p.current_item.image_url),
                         "http://%s:%d%s", s_host, s_port, ipath->valuestring);
            }
        }
    }

    s_player_cb(&p);
}

/* ── Process message ── */
static void process_message(const char *data, int len)
{
    cJSON *json = cJSON_ParseWithLength(data, len);
    if (!json) return;

    ESP_LOGD(TAG, "RX: %.100s", data);

    cJSON *evt = cJSON_GetObjectItem(json, "event");
    cJSON *msg_data = cJSON_GetObjectItem(json, "data");

    if (evt && cJSON_IsString(evt)) {
        const char *e = evt->valuestring;
        if (strcmp(e, "players_updated") == 0 && msg_data && cJSON_IsArray(msg_data)) {
            int n = cJSON_GetArraySize(msg_data);
            for (int i = 0; i < n; i++) {
                cJSON *player = cJSON_GetArrayItem(msg_data, i);
                cJSON *pid = cJSON_GetObjectItem(player, "player_id");
                /* Solo procesar spotify */
                if (pid && cJSON_IsString(pid) &&
                    strstr(pid->valuestring, "spotify") != NULL) {
                    strncpy(s_player_id, pid->valuestring, sizeof(s_player_id)-1);
                    parse_player(player);
                }
            }
        } else if (strcmp(e, "player_updated") == 0 && msg_data) {
            cJSON *pid = cJSON_GetObjectItem(msg_data, "player_id");
            if (pid && cJSON_IsString(pid) && strstr(pid->valuestring, "spotify"))
                parse_player(msg_data);
        }
    }

    /* Respuesta a start_listening — contiene estado inicial */
    cJSON *result = cJSON_GetObjectItem(json, "result");
    if (result) {
        cJSON *players = cJSON_GetObjectItem(result, "players");
        if (players && cJSON_IsArray(players)) {
            int n = cJSON_GetArraySize(players);
            ESP_LOGI(TAG, "MA players recibidos: %d", n);
            for (int i = 0; i < n; i++) {
                cJSON *p = cJSON_GetArrayItem(players, i);
                cJSON *pid = cJSON_GetObjectItem(p, "player_id");
                if (pid && cJSON_IsString(pid)) ESP_LOGI(TAG, "Player: %s", pid->valuestring);
                if (pid && cJSON_IsString(pid) && strstr(pid->valuestring, "spotify")) {
                    strncpy(s_player_id, pid->valuestring, sizeof(s_player_id)-1);
                    parse_player(p);
                    ESP_LOGI(TAG, "Spotify player: %s", s_player_id);
                }
            }
        }
    }

    cJSON_Delete(json);
}

/* ── WebSocket event handler ── */
static void ws_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MA WebSocket conectado");
        s_connected = true;
        ma_authenticate();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MA WebSocket desconectado");
        s_connected = false;
        s_authenticated = false;
        if (s_connected_cb) s_connected_cb(false);
        break;
    case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG, "MA raw: %.200s", (char*)data->data_ptr);
        if (data->data_ptr && data->data_len > 0)
            process_message(data->data_ptr, data->data_len);
        break;
    default: break;
    }
}

/* ── Send command ── */
static esp_err_t ma_command(const char *cmd, const char *player_id, cJSON *extra)
{
    if (!s_authenticated) return ESP_ERR_INVALID_STATE;
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "message_id", s_msg_id++);
    cJSON_AddStringToObject(msg, "command", cmd);
    if (player_id) cJSON_AddStringToObject(msg, "player_id", player_id);
    if (extra) {
        cJSON *item;
        cJSON_ArrayForEach(item, extra) {
            cJSON_AddItemToObject(msg, item->string, cJSON_Duplicate(item, true));
        }
    }
    ma_send(msg);
    cJSON_Delete(msg);
    if (extra) cJSON_Delete(extra);
    return ESP_OK;
}

/* ── Public API ── */
esp_err_t ma_play_pause(const char *player_id)
{
    const char *pid = player_id ? player_id : s_player_id;
    return ma_command("player_command_play_pause", pid, NULL);
}

esp_err_t ma_next(const char *player_id)
{
    const char *pid = player_id ? player_id : s_player_id;
    return ma_command("player_command_next", pid, NULL);
}

esp_err_t ma_prev(const char *player_id)
{
    const char *pid = player_id ? player_id : s_player_id;
    return ma_command("player_command_previous", pid, NULL);
}

esp_err_t ma_seek(const char *player_id, int position)
{
    cJSON *extra = cJSON_CreateObject();
    cJSON_AddNumberToObject(extra, "position", position);
    return ma_command("player_command_seek", player_id ? player_id : s_player_id, extra);
}

esp_err_t ma_set_volume(const char *player_id, int volume)
{
    cJSON *extra = cJSON_CreateObject();
    cJSON_AddNumberToObject(extra, "volume_level", volume);
    return ma_command("player_command_volume_set", player_id ? player_id : s_player_id, extra);
}

esp_err_t ma_get_queue(const char *player_id)
{
    return ma_command("player_queue_items", player_id ? player_id : s_player_id, NULL);
}

bool ma_client_is_connected(void) { return s_authenticated; }
void ma_client_set_player_cb(ma_player_cb_t cb) { s_player_cb = cb; }
void ma_client_set_connected_cb(ma_connected_cb_t cb) { s_connected_cb = cb; }

esp_err_t ma_client_init(const char *host, uint16_t port, const char *token)
{
    strncpy(s_host, host, sizeof(s_host)-1);
    strncpy(s_token, token, sizeof(s_token)-1);
    s_port = port;
    return ESP_OK;
}

esp_err_t ma_client_start(void)
{
    char uri[256];
    snprintf(uri, sizeof(uri), "ws://%s:%d/ws?token=%s", s_host, s_port, s_token);
    ESP_LOGI(TAG, "Conectando a MA: ws://%s:%d/ws", s_host, s_port);

    esp_websocket_client_config_t ws_cfg = {
        .uri                  = uri,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 15000,
        .buffer_size          = 8192,
        .task_stack           = 8192,
    };

    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) return ESP_FAIL;

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    return esp_websocket_client_start(s_client);
}
