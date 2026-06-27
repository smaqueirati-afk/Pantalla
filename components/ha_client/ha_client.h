#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Estado de una entidad HA */
typedef struct {
    char entity_id[64];
    char state[32];        /* "on", "off", "playing", "paused", etc */
    char friendly_name[64];
    /* Media player */
    char media_title[128];
    char media_artist[128];
    char image_url[256];
    int  media_duration;
    int  media_position;
    int  volume_level;     /* 0-100 */
    /* Light */
    int  brightness;       /* 0-255 */
} ha_entity_t;

/* Callbacks */
typedef void (*ha_state_cb_t)(const ha_entity_t *entity);
typedef void (*ha_connected_cb_t)(bool connected);

/* API */
esp_err_t ha_client_init(const char *host, uint16_t port, const char *token);
esp_err_t ha_client_start(void);
void      ha_client_set_state_cb(ha_state_cb_t cb);
void      ha_client_set_connected_cb(ha_connected_cb_t cb);

/* Acciones */
esp_err_t ha_light_toggle(const char *entity_id);
esp_err_t ha_light_set(const char *entity_id, bool on, int brightness);
esp_err_t ha_media_play_pause(const char *entity_id);
esp_err_t ha_media_next(const char *entity_id);
esp_err_t ha_media_prev(const char *entity_id);
esp_err_t ha_media_set_volume(const char *entity_id, float volume);

/* Estado actual */
bool ha_client_is_connected(void);
bool ha_get_entity(const char *entity_id, ha_entity_t *out);

void ha_client_reconnect(void);
void ha_client_set_token(const char *token);

#ifdef __cplusplus
}
#endif
esp_err_t ha_service_call(const char *domain, const char *service, const char *json_body);

