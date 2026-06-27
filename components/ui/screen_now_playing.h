#pragma once
/**
 * @file screen_now_playing.h
 * @brief Now Playing screen — main UI with album art, track info, controls.
 */
#include "lvgl.h"
#include "state/app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_now_playing_create(void);

/* Callbacks from ui_main event processing (called with LVGL lock held) */
void screen_now_playing_on_track_changed(void);
void screen_now_playing_on_playback_state(playback_state_t state);
void screen_now_playing_on_progress(uint32_t position_ms);
void screen_now_playing_on_album_art_ready(void);
void screen_now_playing_on_palette_changed(void);

#ifdef __cplusplus
}
#endif
