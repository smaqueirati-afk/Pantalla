#pragma once
#include "lvgl.h"
#include "ma_client.h"

lv_obj_t *screen_music_create(lv_event_cb_t back_cb);
void screen_music_update(const ma_player_t *p);
void screen_music_add_queue_item(const char *title, const char *artist, int index);
void screen_music_reset_playlists(void);
void screen_music_reset_artwork(void);
void screen_music_set_volume(int vol);
void screen_music_add_playlist_item(const char *name, const char *uri);
void screen_music_add_playlist_item(const char *name, const char *uri);




