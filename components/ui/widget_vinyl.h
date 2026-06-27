#pragma once
/**
 * @file widget_vinyl.h
 * @brief Vinyl disc overlay widget — decorative rotating ring over album art.
 *
 * Renders concentric rings + a centre spindle over the album art container.
 * Rotation is driven by an LVGL animation (lv_anim) for smooth 60fps.
 * 0.5 RPM = 3°/s.  At 60fps → 0.05°/frame → angle step = 5 (LVGL 0.1°).
 */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attach vinyl overlay onto an existing album-art container.
 * @param art_cont  The rounded album-art lv_obj_t.
 * @param size      Diameter in pixels (same as art container size).
 */
void widget_vinyl_create(lv_obj_t *art_cont, int32_t size);

/** Start/resume rotation (call on PLAYBACK_PLAYING). */
void widget_vinyl_play(void);

/** Pause rotation (call on PLAYBACK_PAUSED / STOPPED). */
void widget_vinyl_pause(void);

/** Update the accent ring colour from the current palette. */
void widget_vinyl_set_color(lv_color_t color);

#ifdef __cplusplus
}
#endif
