#pragma once
#include "lvgl.h"

lv_obj_t *screen_audio_create(lv_event_cb_t back_cb);
void screen_audio_set_volume(int vol);
void screen_audio_update_eq(void);
lv_obj_t *screen_audio_get_screen(void);
void screen_audio_set_back_cb(lv_event_cb_t cb);


