#pragma once
/**
 * @file widget_fft.h
 * @brief 32-bar FFT visualiser widget for LVGL.
 *
 * Renders 32 frequency bars with gradient colour (green→yellow→red)
 * and smooth lerp interpolation between frames at 60fps.
 *
 * Usage:
 *   widget_fft_create(parent, width, height);   // once at screen build
 *   widget_fft_update(magnitudes, 32);           // from fft_task via event
 */

#pragma once
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the FFT bar visualiser inside a parent container.
 * @param parent  LVGL parent object (must have fixed size).
 * @param w       Width  in pixels (usually screen width).
 * @param h       Height in pixels (FFT panel height).
 */
void widget_fft_create(lv_obj_t *parent, int32_t w, int32_t h);

/**
 * @brief Feed new FFT magnitudes (called from audio/fft task).
 *        Values are in range 0–255.  Thread-safe (posts to LVGL via timer).
 * @param magnitudes  Array of 32 uint8_t magnitude values.
 * @param count       Must be 32.
 */
void widget_fft_update(const uint8_t *magnitudes, uint8_t count);

/** Show or hide the widget (e.g. hide during screensaver). */
void widget_fft_set_visible(bool visible);

#ifdef __cplusplus
}
#endif
