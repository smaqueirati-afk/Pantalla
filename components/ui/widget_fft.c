/**
 * @file widget_fft.c
 * @brief 32-bar FFT visualiser — LVGL v9 compatible, 60fps with lerp smoothing.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "ui/widget_fft.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "widget_fft";

#define NUM_BARS        32
#define LERP_FACTOR     0.30f
#define PEAK_HOLD_MS    600
#define PEAK_FALL_RATE  0.005f

static lv_obj_t        *s_canvas    = NULL;
static uint8_t         *s_cbuf      = NULL;
static float            s_bars[NUM_BARS]      = {0};
static float            s_targets[NUM_BARS]   = {0};
static float            s_peaks[NUM_BARS]     = {0};
static uint32_t         s_peak_hold[NUM_BARS] = {0};
static SemaphoreHandle_t s_mutex = NULL;
static lv_timer_t       *s_anim_timer = NULL;
static int32_t s_w = 0, s_h = 0;

/* Colour: 0.0=green, 0.5=yellow, 1.0=red */
static lv_color_t bar_colour(float norm)
{
    uint8_t r, g, b = 0;
    if (norm < 0.5f) {
        float t = norm * 2.0f;
        r = (uint8_t)(t * 255);
        g = 220;
    } else {
        float t = (norm - 0.5f) * 2.0f;
        r = 255;
        g = (uint8_t)((1.0f - t) * 220);
    }
    return lv_color_make(r, g, b);
}

static void fft_anim_cb(lv_timer_t *timer)
{
    if (!s_canvas || !s_cbuf) return;

    uint32_t now_ms = (uint32_t)(lv_tick_get());
    float bars_snap[NUM_BARS];
    float peaks_snap[NUM_BARS];

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < NUM_BARS; i++) {
        s_bars[i] += (s_targets[i] - s_bars[i]) * LERP_FACTOR;
        if (s_bars[i] >= s_peaks[i]) {
            s_peaks[i]     = s_bars[i];
            s_peak_hold[i] = now_ms;
        } else if ((now_ms - s_peak_hold[i]) > PEAK_HOLD_MS) {
            s_peaks[i] -= PEAK_FALL_RATE;
            if (s_peaks[i] < 0.0f) s_peaks[i] = 0.0f;
        }
        bars_snap[i] = s_bars[i];
    }
    memcpy(peaks_snap, s_peaks, sizeof(s_peaks));
    xSemaphoreGive(s_mutex);

    /* Clear canvas with dark background */
    lv_canvas_fill_bg(s_canvas, lv_color_make(0x08, 0x08, 0x0C), LV_OPA_COVER);

    int32_t bar_w = s_w / NUM_BARS;
    int32_t gap   = 2;
    int32_t bw    = bar_w - gap;
    int32_t max_h = s_h - 6;

    /* LVGL v9: use lv_draw_rect_dsc_t with lv_canvas_draw_rect replaced by
     * drawing via layer — use simple pixel-based approach for compatibility */
    /* LVGL v8: lv_canvas_draw_rect(canvas, x, y, w, h, dsc) */
    lv_draw_rect_dsc_t rect_dsc;

    for (int i = 0; i < NUM_BARS; i++) {
        float norm = bars_snap[i];
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;

        int32_t bh = (int32_t)(norm * max_h);
        if (bh < 2) bh = 2;

        int32_t x = i * bar_w + gap / 2;
        int32_t y = s_h - bh - 2;

        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color     = bar_colour(norm);
        rect_dsc.bg_opa       = LV_OPA_COVER;
        rect_dsc.radius       = 2;
        rect_dsc.border_width = 0;

        lv_canvas_draw_rect(s_canvas, x, y, bw, bh, &rect_dsc);

        /* Peak dot */
        float pnorm = peaks_snap[i];
        if (pnorm > 0.02f) {
            int32_t py = s_h - (int32_t)(pnorm * max_h) - 4;
            lv_draw_rect_dsc_t pk_dsc;
            lv_draw_rect_dsc_init(&pk_dsc);
            pk_dsc.bg_color     = lv_color_make(0xFF, 0xFF, 0xFF);
            pk_dsc.bg_opa       = LV_OPA_80;
            pk_dsc.radius       = 1;
            pk_dsc.border_width = 0;
            lv_canvas_draw_rect(s_canvas, x, py, bw, 3, &pk_dsc);
        }
    }
}

void widget_fft_create(lv_obj_t *parent, int32_t w, int32_t h)
{
    s_w = w;
    s_h = h;

    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    size_t buf_size = (size_t)w * (size_t)h * sizeof(lv_color_t);
    s_cbuf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!s_cbuf) {
        ESP_LOGE(TAG, "FFT canvas alloc failed (%u bytes)", (unsigned)buf_size);
        return;
    }
    memset(s_cbuf, 0, buf_size);

    s_canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(s_canvas, s_cbuf, (int32_t)w, (int32_t)h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_canvas, w, h);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(s_canvas, lv_color_make(0x08, 0x08, 0x0C), LV_OPA_COVER);

    memset(s_bars,      0, sizeof(s_bars));
    memset(s_targets,   0, sizeof(s_targets));
    memset(s_peaks,     0, sizeof(s_peaks));
    memset(s_peak_hold, 0, sizeof(s_peak_hold));

    s_anim_timer = lv_timer_create(fft_anim_cb, 16, NULL);

    ESP_LOGI(TAG, "FFT widget %dx%d, buf %u bytes PSRAM", (int)w, (int)h, (unsigned)buf_size);
}

void widget_fft_update(const uint8_t *magnitudes, uint8_t count)
{
    if (!s_mutex || count != NUM_BARS) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < NUM_BARS; i++) {
        s_targets[i] = magnitudes[i] / 255.0f;
    }
    xSemaphoreGive(s_mutex);
}

void widget_fft_set_visible(bool visible)
{
    if (!s_canvas) return;
    /* LVGL v9: use flags instead of lv_obj_set_hidden */
    if (visible) {
        lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_anim_timer) {
        if (visible) lv_timer_resume(s_anim_timer);
        else         lv_timer_pause(s_anim_timer);
    }
}
