#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTIFY_MAX_ITEMS 20

typedef struct {
    char id[64];
    char name[128];
    char artist[128];
    char album[128];
    char image_url[256];
    int  duration_ms;
} spotify_track_t;

typedef struct {
    char id[64];
    char name[128];
    char image_url[256];
    int  total_tracks;
} spotify_playlist_t;

typedef struct {
    spotify_track_t track;
    bool   is_playing;
    int    progress_ms;
    char   device_name[64];
} spotify_playback_t;

typedef void (*spotify_playback_cb_t)(const spotify_playback_t *pb);
typedef void (*spotify_playlists_cb_t)(const spotify_playlist_t *list, int count);
typedef void (*spotify_tracks_cb_t)(const spotify_track_t *list, int count);

esp_err_t spotify_client_init(const char *ha_host, uint16_t ha_port,
                               const char *ha_token, const char *device_id);
esp_err_t spotify_client_start(void);
void      spotify_set_playback_cb(spotify_playback_cb_t cb);
void      spotify_set_playlists_cb(spotify_playlists_cb_t cb);
void      spotify_set_tracks_cb(spotify_tracks_cb_t cb);

/* Playback control */
esp_err_t spotify_play_pause(void);
esp_err_t spotify_next(void);
esp_err_t spotify_prev(void);
esp_err_t spotify_seek(int position_ms);
esp_err_t spotify_set_volume(int volume_pct);
esp_err_t spotify_play_uri(const char *uri);

/* Browse */
esp_err_t spotify_get_playlists(void);
esp_err_t spotify_get_playlist_tracks(const char *playlist_id);
esp_err_t spotify_search(const char *query);
esp_err_t spotify_get_current_playback(void);

#ifdef __cplusplus
}
#endif

/* Exchange OAuth code for tokens */
esp_err_t spotify_exchange_code(const char *code,
                                 char *access_token, size_t at_len,
                                 char *refresh_token, size_t rt_len);
