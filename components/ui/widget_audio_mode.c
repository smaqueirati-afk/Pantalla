#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_check.h"
/**
 * @file widget_audio_mode.c
 * @brief Audio mode selector — Internal / Bluetooth / Both.
 */

#include "ui/widget_audio_mode.h"
#include "state/app_state.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "widget_audio_mode";

#define BTN_SIZE   44
#define BTN_GAP    8
#define BTN_RADIUS 10

/* Colours */
#define COL_INACTIVE  lv_color_make(0x2A, 0x2A, 0x35)
#define COL_ACTIVE    lv_color_make(0x1D, 0xB9, 0x54)
#define COL_TEXT      lv_color_make(0xFF, 0xFF, 0xFF)
#define COL_TEXT_DIM  lv_color_make(0x88, 0x88, 0x88)

static lv_obj_t *s_btns[3] = {NULL};   /* Internal, BT, Both */

/* Icons (LVGL built-ins as fallback — ideally replace with MDI glyphs) */
static const char *s_icons[3] = {
    LV_SYMBOL_VOLUME_MAX,   /* Internal speaker */
    LV_SYMBOL_BLUETOOTH,    /* Bluetooth        */
    LV_SYMBOL_AUDIO,        /* Both             */
};
static const char *s_labels[3] = {"INT", "BT", "ALL"};

/* ── Highlight active button ────────────────────────────────────────────────*/
static void highlight(audio_mode_t mode)
{
    for (int i = 0; i < 3; i++) {
        if (!s_btns[i]) continue;
        bool active = ((int)mode == i);
        lv_obj_set_style_bg_color(s_btns[i], active ? COL_ACTIVE : COL_INACTIVE, 0);
        lv_obj_t *ic  = lv_obj_get_child(s_btns[i], 0);
        lv_obj_t *lbl = lv_obj_get_child(s_btns[i], 1);
        if (ic)  lv_obj_set_style_text_color(ic,  active ? COL_TEXT : COL_TEXT_DIM, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, active ? COL_TEXT : COL_TEXT_DIM, 0);
    }
}

/* ── Button callback ────────────────────────────────────────────────────────*/
static void btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    audio_mode_t mode = (audio_mode_t)idx;
    app_state_set_audio_mode(mode);
    highlight(mode);
    ESP_LOGI(TAG, "Audio mode → %d", idx);
}

/* ── Public API ─────────────────────────────────────────────────────────────*/

void widget_audio_mode_create(lv_obj_t *parent)
{
    int32_t total_w = 3 * BTN_SIZE + 2 * BTN_GAP;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, total_w, BTN_SIZE + 24);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    audio_mode_t cur = app_state_get()->audio_mode;

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(cont);
        lv_obj_set_size(btn, BTN_SIZE, BTN_SIZE);
        lv_obj_set_pos(btn, i * (BTN_SIZE + BTN_GAP), 0);
        lv_obj_set_style_radius(btn, BTN_RADIUS, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 4, 0);
        lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_btns[i] = btn;

        /* Icon */
        lv_obj_t *ic = lv_label_create(btn);
        lv_label_set_text(ic, s_icons[i]);
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 0);

        /* Short label below icon */
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, s_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lbl, i * (BTN_SIZE + BTN_GAP) + BTN_SIZE/2 - 8, BTN_SIZE + 2);
    }

    highlight(cur);
    ESP_LOGI(TAG, "Audio mode widget created (current mode=%d)", (int)cur);
}

void widget_audio_mode_refresh(void)
{
    highlight(app_state_get()->audio_mode);
}
