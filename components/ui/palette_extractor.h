#pragma once
/**
 * @file palette_extractor.h
 * @brief Median Cut colour quantisation for album art palette extraction.
 *
 * Input:  RGB888 pixel buffer (decoded album art in PSRAM)
 * Output: color_palette_t with primary, secondary, accent, text_on colours.
 *
 * Algorithm: Median Cut (2 iterations → 4 buckets).
 * Then:
 *   - Primary   = most frequent bucket by pixel count
 *   - Secondary = second most frequent
 *   - Accent    = highest saturation bucket
 *   - text_on   = white or black depending on primary luminance
 *
 * Designed to run on a downsampled version of the art (every 4th pixel)
 * so it's fast enough on the P4 (~5ms for 400x400 source).
 */

#include "state/app_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract a 4-colour palette from an RGB888 bitmap.
 *
 * @param rgb888   Pointer to pixel data (R,G,B bytes, row-major).
 * @param width    Image width in pixels.
 * @param height   Image height in pixels.
 * @param out      Output palette (populated on success).
 * @return true on success, false if input is NULL or too small.
 */
bool palette_extract(const uint8_t *rgb888, uint32_t width, uint32_t height,
                     color_palette_t *out);

#ifdef __cplusplus
}
#endif
