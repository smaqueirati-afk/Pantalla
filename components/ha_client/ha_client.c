/**
 * @file ha_client.c
 * @brief Home Assistant WebSocket client
 * Conecta a ws://192.168.1.100:8123/api/websocket
 * Autentica con token de larga duración
 * Suscribe a state_changed para entidades configuradas
 */

#include "ha_client.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ha_client";

#define HA_MAX_ENTITIES  16
#define HA_MSG_BUF_SIZE  4096

/* Entidades a trackear */
static const char *HA_ENTITIES[] = {
    "media_player.spotify_premiun",
    "media_player.mi_smart_speaker3614",
    "media_player.echo_spot_de_sebastian",
    "switch.luz_oficina_interruptor_1",
    "switch.luz_oficina_interruptor_2",
    "switch.luz_oficina_interruptor_3",
    "light.efectos_de_luz_tira_de_led_tira_led_oficina",
    "light.tv_1",
    "light.tv",
    "light.pc",
    "sensor.speedtest_descarga",
    "sensor.speedtest_subida",
    "sensor.speedtest_ping",
    NULL
};

static esp_websocket_client_handle_t s_client = NULL;
static ha_state_cb_t      s_state_cb     = NULL;
static ha_connected_cb_t  s_connected_cb = NULL;
static bool               s_connected    = false;
static bool               s_authenticated = false;
static int                s_msg_id       = 1;
static char               s_token[512]   = {0};
static char               s_host[64]     = {0};
static uint16_t           s_port         = 8123;

static ha_entity_t s_entities[HA_MAX_ENTITIES] = {0};
static int         s_entity_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

/* ── Entity cache ── */
static ha_entity_t *find_or_create_entity(const char *entity_id)
{
    for (int i = 0; i < s_entity_count; i++) {
        if (strcmp(s_entities[i].entity_id, entity_id) == 0)
            return &s_entities[i];
    }
    if (s_entity_count < HA_MAX_ENTITIES) {
        ha_entity_t *e = &s_entities[s_entity_count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->entity_id, entity_id, sizeof(e->entity_id)-1);
        return e;
    }
    return NULL;
}

bool ha_get_entity(const char *entity_id, ha_entity_t *out)
{
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100));
    for (int i = 0; i < s_entity_count; i++) {
        if (strcmp(s_entities[i].entity_id, entity_id) == 0) {
            *out = s_entities[i];
            xSemaphoreGive(s_mutex);
            return true;
        }
    }
    xSemaphoreGive(s_mutex);
    return false;
}

/* ── Parse state ── */
static void parse_state(const char *entity_id, cJSON *state_obj)
{
    if (!state_obj) return;

    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100));
    ha_entity_t *e = find_or_create_entity(entity_id);
    if (!e) { xSemaphoreGive(s_mutex); return; }

    cJSON *state = cJSON_GetObjectItem(state_obj, "state");
    if (state && cJSON_IsString(state))
        strncpy(e->state, state->valuestring, sizeof(e->state)-1);

    cJSON *attrs = cJSON_GetObjectItem(state_obj, "attributes");
    if (attrs) {
        cJSON *fn = cJSON_GetObjectItem(attrs, "friendly_name");
        if (fn && cJSON_IsString(fn))
            strncpy(e->friendly_name, fn->valuestring, sizeof(e->friendly_name)-1);

        /* Media player */
        cJSON *title = cJSON_GetObjectItem(attrs, "media_title");
        if (title && cJSON_IsString(title))
            strncpy(e->media_title, title->valuestring, sizeof(e->media_title)-1);

        cJSON *artist = cJSON_GetObjectItem(attrs, "media_artist");
        if (artist && cJSON_IsString(artist))
            strncpy(e->media_artist, artist->valuestring, sizeof(e->media_artist)-1);

        cJSON *vol = cJSON_GetObjectItem(attrs, "volume_level");
        if (vol && cJSON_IsNumber(vol))
            e->volume_level = (int)(vol->valuedouble * 100);

        cJSON *pic = cJSON_GetObjectItem(attrs, "entity_picture");
        if (pic && cJSON_IsString(pic)) {
            /* Build full URL: http://192.168.1.100:8123 + entity_picture */
            if (pic->valuestring[0] == '/') {
                snprintf(e->image_url, sizeof(e->image_url),
                    "http://%s:8123%s", s_host, pic->valuestring);
            } else {
                strncpy(e->image_url, pic->valuestring, sizeof(e->image_url)-1);
            }
        }

        cJSON *dur = cJSON_GetObjectItem(attrs, "media_duration");
        if (dur && cJSON_IsNumber(dur))
            e->media_duration = (int)dur->valuedouble;

        cJSON *pos = cJSON_GetObjectItem(attrs, "media_position");
        if (pos && cJSON_IsNumber(pos))
            e->media_position = (int)pos->valuedouble;

        /* Light */
        cJSON *bright = cJSON_GetObjectItem(attrs, "brightness");
        if (bright && cJSON_IsNumber(bright))
            e->brightness = (int)bright->valuedouble;
    }

    ha_entity_t copy = *e;
    xSemaphoreGive(s_mutex);

    if (s_state_cb) s_state_cb(&copy);
    ESP_LOGD(TAG, "State: %s = %s", entity_id, copy.state);


}

/* ── Send JSON ── */
static void ha_send(cJSON *json)
{
    if (!s_client || !s_connected) return;
    char *str = cJSON_PrintUnformatted(json);
    if (!str) return;
    esp_websocket_client_send_text(s_client, str, strlen(str), pdMS_TO_TICKS(3000));
    free(str);
}

/* ── Auth ── */
static void ha_authenticate(void)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "auth");
    cJSON_AddStringToObject(msg, "access_token", s_token);
    ha_send(msg);
    cJSON_Delete(msg);
}

/* ── Subscribe to state_changed ── */
static void ha_subscribe_states(void)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id", s_msg_id++);
    cJSON_AddStringToObject(msg, "type", "subscribe_events");
    cJSON_AddStringToObject(msg, "event_type", "state_changed");
    ha_send(msg);
    cJSON_Delete(msg);
    ESP_LOGI(TAG, "Suscrito a state_changed");
}

/* ── Get initial states ── */
static void ha_get_states(void)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id", s_msg_id++);
    cJSON_AddStringToObject(msg, "type", "get_states");
    ha_send(msg);
    cJSON_Delete(msg);
}

/* ── Process message ── */
static void process_message(const char *data, int len)
{
    cJSON *json = cJSON_ParseWithLength(data, len);
    if (!json) return;

    cJSON *type = cJSON_GetObjectItem(json, "type");
    if (!type || !cJSON_IsString(type)) { cJSON_Delete(json); return; }

    const char *t = type->valuestring;

    if (strcmp(t, "auth_required") == 0) {
        ESP_LOGI(TAG, "Auth required — enviando token");
        ha_authenticate();

    } else if (strcmp(t, "auth_ok") == 0) {
        ESP_LOGI(TAG, "Autenticado en HA!");
        s_authenticated = true;
        ha_get_states();
        ha_subscribe_states();
        if (s_connected_cb) s_connected_cb(true);

    } else if (strcmp(t, "auth_invalid") == 0) {
        ESP_LOGE(TAG, "Token invalido!");
        if (s_connected_cb) s_connected_cb(false);

    } else if (strcmp(t, "result") == 0) {
        /* Respuesta a get_states — array de estados */
        cJSON *result = cJSON_GetObjectItem(json, "result");
        if (result && cJSON_IsArray(result)) {
            int count = cJSON_GetArraySize(result);
            for (int i = 0; i < count; i++) {
                cJSON *item = cJSON_GetArrayItem(result, i);
                cJSON *eid = cJSON_GetObjectItem(item, "entity_id");
                if (!eid || !cJSON_IsString(eid)) continue;
                /* Solo procesar entidades que nos interesan */
                for (int j = 0; HA_ENTITIES[j]; j++) {
                    if (strcmp(eid->valuestring, HA_ENTITIES[j]) == 0) {
                        parse_state(eid->valuestring, item);
                        break;
                    }
                }
            }
        }

    } else if (strcmp(t, "event") == 0) {
        /* state_changed event */
        cJSON *event = cJSON_GetObjectItem(json, "event");
        if (!event) { cJSON_Delete(json); return; }
        cJSON *edata = cJSON_GetObjectItem(event, "data");
        if (!edata) { cJSON_Delete(json); return; }
        cJSON *eid = cJSON_GetObjectItem(edata, "entity_id");
        cJSON *new_state = cJSON_GetObjectItem(edata, "new_state");
        if (eid && cJSON_IsString(eid) && new_state) {
            /* Solo entidades que nos interesan */
            for (int j = 0; HA_ENTITIES[j]; j++) {
                if (strcmp(eid->valuestring, HA_ENTITIES[j]) == 0) {
                    parse_state(eid->valuestring, new_state);
                    break;
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
        ESP_LOGI(TAG, "WebSocket conectado");
        s_connected = true;
        s_authenticated = false;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket desconectado");
        s_connected = false;
        s_authenticated = false;
        if (s_connected_cb) s_connected_cb(false);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_ptr && data->data_len > 0) {
            process_message(data->data_ptr, data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

/* ── Call service ── */
static esp_err_t ha_call_service(const char *domain, const char *service,
                                  const char *entity_id, cJSON *extra)
{
    if (!s_authenticated) return ESP_ERR_INVALID_STATE;

    cJSON *msg = cJSON_CreateObject();
    cJSON_AddNumberToObject(msg, "id", s_msg_id++);
    cJSON_AddStringToObject(msg, "type", "call_service");
    cJSON_AddStringToObject(msg, "domain", domain);
    cJSON_AddStringToObject(msg, "service", service);

    cJSON *target = cJSON_CreateObject();
    cJSON_AddStringToObject(target, "entity_id", entity_id);
    cJSON_AddItemToObject(msg, "target", target);

    if (extra) cJSON_AddItemToObject(msg, "service_data", extra);

    ha_send(msg);
    cJSON_Delete(msg);
    return ESP_OK;
}

/* ── Public API ── */
esp_err_t ha_light_toggle(const char *entity_id) {
    /* Detectar si es switch o light */
    if (strncmp(entity_id, "switch.", 7) == 0) {
        return ha_call_service("switch", "toggle", entity_id, NULL);
    }
    return ha_call_service("light", "toggle", entity_id, NULL);
}

esp_err_t ha_light_set(const char *entity_id, bool on, int brightness) {
    const char *domain = (strncmp(entity_id, "switch.", 7) == 0) ? "switch" : "light";
    cJSON *data = cJSON_CreateObject();
    if (brightness >= 0 && strcmp(domain, "light") == 0)
        cJSON_AddNumberToObject(data, "brightness", brightness);
    return ha_call_service(domain, on ? "turn_on" : "turn_off", entity_id, data);
}

esp_err_t ha_media_play_pause(const char *entity_id) {
    return ha_call_service("media_player", "media_play_pause", entity_id, NULL);
}

esp_err_t ha_media_next(const char *entity_id) {
    return ha_call_service("media_player", "media_next_track", entity_id, NULL);
}

esp_err_t ha_media_prev(const char *entity_id) {
    return ha_call_service("media_player", "media_previous_track", entity_id, NULL);
}

esp_err_t ha_media_set_volume(const char *entity_id, float volume) {
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "volume_level", volume);
    return ha_call_service("media_player", "volume_set", entity_id, data);
}

bool ha_client_is_connected(void) { return s_authenticated; }

void ha_client_set_state_cb(ha_state_cb_t cb)     { s_state_cb = cb; }
void ha_client_set_connected_cb(ha_connected_cb_t cb) { s_connected_cb = cb; }

esp_err_t ha_client_init(const char *host, uint16_t port, const char *token)
{
    strncpy(s_host,  host,  sizeof(s_host)-1);
    strncpy(s_token, token, sizeof(s_token)-1);
    s_port = port;
    s_mutex = xSemaphoreCreateMutex();
    return ESP_OK;
}

esp_err_t ha_client_start(void)
{
    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/api/websocket", s_host, s_port);
    ESP_LOGI(TAG, "Conectando a %s", uri);

    esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
        .buffer_size          = 4096,
        .ping_interval_sec    = 20,
    };

    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) return ESP_FAIL;

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, NULL);
    return esp_websocket_client_start(s_client);
}

void ha_client_reconnect(void) {
    ESP_LOGI(TAG, "Forzando reconexion HA...");
    if (s_client) {
        esp_websocket_client_stop(s_client);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_websocket_client_start(s_client);
    }
}

/* Update token and reconnect */
void ha_client_set_token(const char *token)
{
    if (!token || strlen(token) < 10) return;
    strncpy(s_token, token, sizeof(s_token)-1);
    ESP_LOGI(TAG, "Token actualizado, reconectando...");
    /* Force reconnect by stopping and restarting */
    if (s_client) {
        esp_websocket_client_stop(s_client);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_websocket_client_start(s_client);
    }
}

esp_err_t ha_service_call(const char *domain, const char *service, const char *json_body)
{
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/api/services/%s/%s", s_host, s_port, domain, service);
    char auth[600];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);

    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 60000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    const char *body = json_body ? json_body : "{}";
    esp_http_client_set_post_field(c, body, strlen(body));
    esp_err_t ret = esp_http_client_perform(c);
    esp_http_client_cleanup(c);
    return ret;
}



