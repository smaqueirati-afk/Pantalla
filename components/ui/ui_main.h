#pragma once
/**
 * @file ui_main.h
 * @brief LVGL task orchestration and screen navigation.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_NOW_PLAYING = 0,
    SCREEN_QUEUE,
    SCREEN_EQ,
    SCREEN_BT_MANAGER,
    SCREEN_SETTINGS,
    SCREEN_SCREENSAVER,
} screen_id_t;

/** Spawn the LVGL render task on core 1. Call once from app_main. */
void ui_start(void);

/** Navigate to a screen with animated transition. Thread-safe. */
void ui_navigate(screen_id_t screen);

/** Get currently active screen. */
screen_id_t ui_current_screen(void);

#ifdef __cplusplus
}
#endif
