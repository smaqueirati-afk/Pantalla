#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_check.h"
/**
 * @file widget_vinyl.c
 * @brief Vinyl overlay widget implementation.
 *
 * Strategy: we don't actually rotate the album art image (expensive).
 * Instead we draw a transparent overlay with:
 *   - A dark semi-transparent ring around the art perimeter (vinyl groove effect)
 *   - Several concentric arcs at different radii
 *   - A white spindle dot in the centre
 *
 * Rotation is applied to the outer ring container via lv_anim changing
 * the transform_rotation style property.  The centre dot is excluded
 * from rotation (it's a separate child on top).
 *
 * 0.5 RPM = 3°/s.  lv_anim runs in 360° / 3°s = 120 seconds per full turn.
 * We use exec_cb to increment transform_rotation by 1 (= 0.1°) every 33ms.
 */

#include "ui/widget_vinyl.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "widget_vinyl";

static lv_obj_t *s_ring    = NULL;   /* rotating outer ring */
static lv_obj_t *s_spindle = NULL;   /* static centre dot   */
static lv_anim_t s_anim    = {0};
static int32_t   s_size    = 0;
static bool      s_running = false;

/* ── Rotation animation ─────────────────────────────────────────────────────*/
static void rotation_exec_cb(void *obj, int32_t angle)
{
    /* angle goes 0 → 3600 (0.0° → 360.0° in LVGL 0.1° units) */
    lv_obj_set_style_transform_angle((lv_obj_t *)obj, (int16_t)(angle % 3600), 0);
}

static void start_anim(void)
{
    if (!s_ring) return;
    lv_anim_init(&s_anim);
    lv_anim_set_var(&s_anim, s_ring);
    lv_anim_set_exec_cb(&s_anim, rotation_exec_cb);
    lv_anim_set_values(&s_anim, 0, 3600);
    /* 120 000 ms = 120s per full revolution = 0.5 RPM */
    lv_anim_set_time(&s_anim, 120000);
    lv_anim_set_repeat_count(&s_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&s_anim, lv_anim_path_linear);
    lv_anim_start(&s_anim);
    s_running = true;
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

void widget_vinyl_create(lv_obj_t *art_cont, int32_t size)
{
    s_size = size;

    /* Rotating ring container — covers full art area, clip disabled so
     * the transform pivot works correctly */
    s_ring = lv_obj_create(art_cont);
    lv_obj_set_size(s_ring, size, size);
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ring, 0, 0);
    lv_obj_set_style_pad_all(s_ring, 0, 0);
    lv_obj_set_style_transform_pivot_x(s_ring, size / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_ring, size / 2, 0);
    lv_obj_set_scrollbar_mode(s_ring, LV_SCROLLBAR_MODE_OFF);

    /* Outer dark rim */
    lv_obj_t *rim = lv_obj_create(s_ring);
    lv_obj_set_size(rim, size, size);
    lv_obj_align(rim, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(rim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rim, 12, 0);
    lv_obj_set_style_border_color(rim, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_border_opa(rim, LV_OPA_50, 0);
    lv_obj_set_style_radius(rim, size / 2, 0);
    lv_obj_set_style_pad_all(rim, 0, 0);

    /* Groove rings — concentric arcs at different radii */
    int32_t radii[] = {size/2 - 30, size/2 - 50, size/2 - 70, size/2 - 90};
    for (int i = 0; i < 4; i++) {
        int32_t r = radii[i];
        if (r <= 0) continue;
        int32_t d = r * 2;
        lv_obj_t *groove = lv_obj_create(s_ring);
        lv_obj_set_size(groove, d, d);
        lv_obj_align(groove, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_opa(groove, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(groove, 1, 0);
        lv_obj_set_style_border_color(groove, lv_color_make(0x00, 0x00, 0x00), 0);
        lv_obj_set_style_border_opa(groove, LV_OPA_30, 0);
        lv_obj_set_style_radius(groove, r, 0);
        lv_obj_set_style_pad_all(groove, 0, 0);
    }

    /* Accent ring (coloured — updated from palette) */
    int32_t accent_d = size - 20;
    lv_obj_t *accent = lv_obj_create(s_ring);
    lv_obj_set_size(accent, accent_d, accent_d);
    lv_obj_align(accent, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(accent, 2, 0);
    lv_obj_set_style_border_color(accent, lv_color_make(0x1D, 0xB9, 0x54), 0);
    lv_obj_set_style_border_opa(accent, LV_OPA_40, 0);
    lv_obj_set_style_radius(accent, accent_d / 2, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);

    /* Static spindle — NOT a child of s_ring so it doesn't rotate */
    s_spindle = lv_obj_create(art_cont);
    lv_obj_set_size(s_spindle, 24, 24);
    lv_obj_align(s_spindle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_spindle, lv_color_make(0xF0, 0xF0, 0xF0), 0);
    lv_obj_set_style_bg_opa(s_spindle, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_spindle, lv_color_make(0xAA, 0xAA, 0xAA), 0);
    lv_obj_set_style_border_width(s_spindle, 2, 0);
    lv_obj_set_style_radius(s_spindle, 12, 0);
    lv_obj_set_style_pad_all(s_spindle, 0, 0);

    ESP_LOGI(TAG, "Vinyl widget created (size=%d)", (int)size);
}

void widget_vinyl_play(void)
{
    if (!s_ring || s_running) return;
    start_anim();
}

void widget_vinyl_pause(void)
{
    if (!s_ring || !s_running) return;
    lv_anim_del(s_ring, rotation_exec_cb);
    s_running = false;
}

void widget_vinyl_set_color(lv_color_t color)
{
    if (!s_ring) return;
    /* Find the accent ring (3rd child at index 2+num_grooves) — simpler to
     * traverse children and update the one with the coloured border */
    uint32_t cnt = lv_obj_get_child_cnt(s_ring);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(s_ring, i);
        lv_opa_t bopa = lv_obj_get_style_border_opa(child, 0);
        if (bopa == LV_OPA_40) {
            lv_obj_set_style_border_color(child, color, 0);
            break;
        }
    }
}
