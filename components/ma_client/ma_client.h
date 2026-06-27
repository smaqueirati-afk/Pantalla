#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char item_id[64];
    char title[128];
    char artist[128];
    char album[128];
    char image_url[256];
    int  duration;
    int  position;
} ma_queue_item_t;

typedef struct {
    char player_id[64];
    char name[64];
    bool playing;
    float volume;
    int  current_index;
    int  elapsed_time;
    ma_queue_item_t current_item;
} ma_player_t;

typedef void (*ma_player_cb_t)(const ma_player_t *player);
typedef void (*ma_connected_cb_t)(bool connected);

esp_err_t ma_client_init(const char *host, uint16_t port, const char *token);
esp_err_t ma_client_start(void);
void      ma_client_set_player_cb(ma_player_cb_t cb);
void      ma_client_set_connected_cb(ma_connected_cb_t cb);
bool      ma_client_is_connected(void);

/* Comandos */
esp_err_t ma_play_pause(const char *player_id);
esp_err_t ma_next(const char *player_id);
esp_err_t ma_prev(const char *player_id);
esp_err_t ma_seek(const char *player_id, int position);
esp_err_t ma_set_volume(const char *player_id, int volume);
esp_err_t ma_get_queue(const char *player_id);

#ifdef __cplusplus
}
#endif
