/**
 * @file bsp_display.h
 * @brief Compatibility shim — mapea las llamadas BSP al BSP oficial de Waveshare
 */
#pragma once

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LCD_H_RES  1024
#define BSP_LCD_V_RES  600

static inline bool bsp_lvgl_lock(uint32_t timeout_ms) {
    return bsp_display_lock(timeout_ms);
}

static inline void bsp_lvgl_unlock(void) {
    bsp_display_unlock();
}

#ifdef __cplusplus
}
#endif
