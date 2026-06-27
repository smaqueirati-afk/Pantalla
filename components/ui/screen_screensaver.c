/**
 * @file screen_screensaver.c
 * @brief Particle screensaver — 80 particles, LVGL v9 Canvas, 60fps.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "ui/screen_screensaver.h"
#include "ui/ui_main.h"
#include "state/app_state.h"
#include "state/event_bus.h"

#include "esp_log.h"
#include "esp_random.h"
#include "lvgl.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "screensaver";

#define SCR_W       1024
#define SCR_H       600
#define NUM_PARTS   80
#define TIMER_MS    16

typedef struct {
    float      x, y;
    float      vx, vy;
    float      radius;
    lv_color_t color;
    lv_opa_t   opa;
    float      life;
    float      life_speed;
    bool       active;
} particle_t;

static lv_obj_t    *s_screen  = NULL;
static lv_obj_t    *s_canvas  = NULL;
static uint8_t     *s_cbuf    = NULL;
static lv_timer_t  *s_timer   = NULL;
static particle_t   s_parts[NUM_PARTS];

static lv_color_t s_pal[3]; /* inicializado en screen_screensaver_create */

static float randf(void)
{
    return (float)(esp_random() & 0xFFFF) / 65535.0f;
}

static float randf_range(float lo, float hi)
{
    return lo + randf() * (hi - lo);
}

static void particle_spawn(particle_t *p)
{
    p->x          = randf_range(0, SCR_W);
    p->y          = randf_range(0, SCR_H);
    float speed   = randf_range(0.2f, 1.2f);
    float angle   = randf_range(0, 6.2832f);
    p->vx         = cosf(angle) * speed;
    p->vy         = sinf(angle) * speed;
    p->radius     = randf_range(2.0f, 6.0f);
    p->opa        = (lv_opa_t)(randf_range(80, 200));
    p->life       = 1.0f;
    p->life_speed = randf_range(0.002f, 0.006f);
    p->color      = s_pal[(esp_random() % 3)];
    p->active     = true;
}

static void timer_cb(lv_timer_t *t)
{
    if (!s_canvas || !s_cbuf) return;

    /* LVGL v8: lv_canvas_draw_rect(canvas, x, y, w, h, dsc) */
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color     = lv_color_make(0x08, 0x08, 0x10);
    bg_dsc.bg_opa       = LV_OPA_20;
    bg_dsc.radius       = 0;
    bg_dsc.border_width = 0;
    lv_canvas_draw_rect(s_canvas, 0, 0, SCR_W, SCR_H, &bg_dsc);

    lv_draw_rect_dsc_t dot_dsc;

    for (int i = 0; i < NUM_PARTS; i++) {
        particle_t *p = &s_parts[i];
        if (!p->active) {
            if ((esp_random() & 0xFF) < 4) particle_spawn(p);
            continue;
        }

        p->x    += p->vx;
        p->y    += p->vy;
        p->life -= p->life_speed;

        float fade = (p->life < 0.2f) ? (p->life / 0.2f) : 1.0f;
        lv_opa_t opa = (lv_opa_t)(p->opa * fade);

        if (p->life <= 0.0f || p->x < -10 || p->x > SCR_W + 10
            || p->y < -10 || p->y > SCR_H + 10) {
            p->active = false;
            continue;
        }

        int32_t r  = (int32_t)p->radius;
        int32_t d  = r * 2;
        int32_t px = (int32_t)p->x - r;
        int32_t py = (int32_t)p->y - r;

        lv_draw_rect_dsc_init(&dot_dsc);
        dot_dsc.bg_color    = p->color;
        dot_dsc.bg_opa      = opa;
        dot_dsc.radius      = r;
        dot_dsc.border_width = 0;
        lv_canvas_draw_rect(s_canvas, px, py, d, d, &dot_dsc);
    }
}

static void screen_touch_cb(lv_event_t *e)
{
    event_post_u32(EVT_UI_TOUCH_ACTIVE, 0);
}

lv_obj_t *screen_screensaver_create(void)
{
    ESP_LOGI(TAG, "Creating screensaver");

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_make(0x05, 0x05, 0x0A), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_add_event_cb(s_screen, screen_touch_cb, LV_EVENT_CLICKED, NULL);

    /* Default palette colours */
    s_pal[0] = lv_color_make(0xB0, 0xB0, 0xB0);
    s_pal[1] = lv_color_make(0x1D, 0xB9, 0x54);
    s_pal[2] = lv_color_make(0x80, 0x40, 0xC0);

    size_t buf_size = (size_t)SCR_W * SCR_H * sizeof(lv_color_t);
    s_cbuf = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!s_cbuf) {
        ESP_LOGE(TAG, "PSRAM alloc failed");
        return s_screen;
    }
    memset(s_cbuf, 0x08, buf_size);

    s_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas, s_cbuf, SCR_W, SCR_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_canvas, SCR_W, SCR_H);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    for (int i = 0; i < NUM_PARTS; i++) {
        particle_spawn(&s_parts[i]);
        s_parts[i].life = randf_range(0.0f, 1.0f);
    }

    s_timer = lv_timer_create(timer_cb, TIMER_MS, NULL);

    screen_screensaver_on_palette_changed();

    ESP_LOGI(TAG, "Screensaver ready — %d particles, %u KB PSRAM",
             NUM_PARTS, (unsigned)(buf_size / 1024));
    return s_screen;
}

void screen_screensaver_on_palette_changed(void)
{
    const app_state_t *st = app_state_get();
    if (!st->palette_ready) return;
    uint32_t p  = st->palette.primary;
    uint32_t ac = st->palette.accent;
    uint32_t sc = st->palette.secondary;
    s_pal[0] = lv_color_make((p >>16)&0xFF, (p >>8)&0xFF, p &0xFF);
    s_pal[1] = lv_color_make((ac>>16)&0xFF, (ac>>8)&0xFF, ac&0xFF);
    s_pal[2] = lv_color_make((sc>>16)&0xFF, (sc>>8)&0xFF, sc&0xFF);
}
