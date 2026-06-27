#pragma once
/**
 * @file widget_audio_mode.h
 * @brief Audio output mode selector widget (Internal / BT / Both).
 *
 * Renders three small icon-buttons in a row.
 * Active mode is highlighted with the accent colour.
 * Tap cycles: Internal → BT → Both → Internal.
 * State persisted via app_state_set_audio_mode() → NVS.
 */
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the mode selector widget inside a parent container.
 *        Positions itself at bottom-right of the parent.
 */
void widget_audio_mode_create(lv_obj_t *parent);

/** Refresh icon highlight after an external mode change event. */
void widget_audio_mode_refresh(void);

#ifdef __cplusplus
}
#endif
