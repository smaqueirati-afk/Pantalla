#pragma once
/**
 * @file screen_screensaver.h
 * @brief Particle screensaver screen — 60fps floating particles.
 */
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_screensaver_create(void);

/** Call when palette changes so particles update their colour range. */
void screen_screensaver_on_palette_changed(void);

#ifdef __cplusplus
}
#endif
