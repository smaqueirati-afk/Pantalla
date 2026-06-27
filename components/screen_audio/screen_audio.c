/**
 * @file screen_audio.c — Audio Console para ESP32-P4
 *       Basada fielmente en audio_console.c del usuario.
 *       Pantalla: 1024x600  LVGL v9.x
 */
#include "screen_audio.h"
#include "ha_client.h"
#include "volumio_client.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>

static const char *TAG = "screen_audio";
static lv_obj_t      *s_screen    = NULL;
static lv_timer_t    *s_anim_timer = NULL;
static lv_event_cb_t  s_back_cb   = NULL;

/* Animación interna */
static int s_spec_h[24]    = {0};
static int s_spec_peak[24] = {0};
static int s_spec_hold[24] = {0};
static int s_vol_pct = 70;

/* Funciones de acción — conectan UI con HA y Pi */
static void audio_apply_mute(bool muted)
{
    float vol = muted ? 0.0f : (float)(s_vol_pct) / 100.0f;
    ha_media_set_volume("media_player.spotify_premiun", vol);
    char path[32];
    snprintf(path, sizeof(path), "/api/volume?vol=%d", muted ? 0 : s_vol_pct);
    volumio_get_path(path);
    ESP_LOGI(TAG, "MUTE: %s", muted ? "ON" : "OFF");
}

/* forward declaration */
static void audio_apply_eq(void *ch_ptr);



/* ── Colores ─────────────────────────────────────────────── */
#define C_BG            lv_color_make(0x0D,0x0D,0x1A)
#define C_TOPBAR        lv_color_make(0x11,0x11,0x2A)
#define C_SURFACE       lv_color_make(0x11,0x11,0x22)
#define C_SURFACE2      lv_color_make(0x1A,0x1A,0x2E)
#define C_BORDER        lv_color_make(0x1E,0x1E,0x3A)
#define C_BORDER2       lv_color_make(0x2A,0x2A,0x4A)
#define C_BORDER3       lv_color_make(0x3A,0x3A,0x6A)
#define C_ACCENT        lv_color_make(0x7F,0x77,0xDD)
#define C_CYAN          lv_color_make(0x00,0xD4,0xFF)
#define C_GREEN         lv_color_make(0x22,0xCC,0x44)
#define C_GREEN_PEAK    lv_color_make(0x44,0xFF,0x66)
#define C_YELLOW        lv_color_make(0xDD,0xAA,0x00)
#define C_ORANGE        lv_color_make(0xDD,0x66,0x00)
#define C_RED           lv_color_make(0xCC,0x22,0x22)
#define C_RED_MUTE      lv_color_make(0xCC,0x44,0x44)
#define C_TEXT          lv_color_make(0xC8,0xC8,0xFF)
#define C_TEXT_MID      lv_color_make(0xA0,0xA0,0xCC)
#define C_TEXT_DIM      lv_color_make(0x50,0x50,0xAA)
#define C_TEXT_SCALE    lv_color_make(0x3A,0x3A,0x6A)
#define C_KNOB          lv_color_make(0xE8,0xE8,0xFF)
#define C_VALUE_BG      lv_color_make(0x0D,0x1A,0x2A)
#define C_ZERO_LINE     lv_color_make(0x3A,0x3A,0x6A)

/* ── Dimensiones ─────────────────────────────────────────── */
#define SCR_W           1024
#define SCR_H           600
#define TOPBAR_H        44
#define CONTENT_H       (SCR_H - TOPBAR_H - 1)
#define LEFT_W          430
#define RIGHT_W         (SCR_W - LEFT_W - 1)
#define SLIDER_H        400
#define SLIDER_W        32
#define KNOB_H          18
#define SCALE_W         24
#define CH_COL_W        120      /* ancho total de cada columna de canal */
#define SPECTRUM_BARS   24
#define VU_H            14

/* ── Estructura interna ──────────────────────────────────── */
typedef struct {
    lv_obj_t *bar;
    lv_obj_t *knob;
    lv_obj_t *lbl_val;
    int16_t   value_db;
} eq_ch_t;

static struct {
    lv_obj_t *btn_mute;
    bool      muted;
    eq_ch_t   master;
    eq_ch_t   bass;
    eq_ch_t   treble;
    lv_obj_t *spec_bars[SPECTRUM_BARS];
    lv_obj_t *spec_peaks[SPECTRUM_BARS];
    lv_obj_t *vu_l, *vu_r;
    lv_obj_t *vu_peak_l, *vu_peak_r;
    lv_obj_t *lbl_vu_l, *lbl_vu_r;
} ac;

/* ── Estilos ─────────────────────────────────────────────── */
static lv_style_t sty_screen;
static lv_style_t sty_topbar;
static lv_style_t sty_nav_btn, sty_nav_btn_pr;
static lv_style_t sty_mute_btn, sty_mute_btn_active, sty_mute_btn_pr;
static lv_style_t sty_divider;
static lv_style_t sty_surface_box;
static lv_style_t sty_slider_bg, sty_slider_ind;
static lv_style_t sty_knob;
static lv_style_t sty_ch_lbl;
static lv_style_t sty_value_lbl;
static lv_style_t sty_scale_lbl;
static lv_style_t sty_section_lbl;
static lv_style_t sty_bar_green, sty_bar_yellow, sty_bar_orange;
static lv_style_t sty_vu_bg, sty_vu_fill;

/* ─────────────────────────────────────────────────────────── */
/*  Callbacks weak                                             */
/* ─────────────────────────────────────────────────────────── *//* ─────────────────────────────────────────────────────────── */
/*  Helpers                                                    */
/* ─────────────────────────────────────────────────────────── */
static inline int32_t db_to_pct(int16_t db)
{
    return (int32_t)(db + 12) * 100 / 24;
}
static inline int16_t pct_to_db(int32_t pct)
{
    return (int16_t)(pct * 24 / 100 - 12);
}
static void update_eq_label(eq_ch_t *ch)
{
    char buf[8];
    snprintf(buf, sizeof(buf), ch->value_db >= 0 ? "+%d" : "%d", ch->value_db);
    lv_label_set_text(ch->lbl_val, buf);
}
static void update_eq_visual(eq_ch_t *ch)
{
    int32_t    pct   = db_to_pct(ch->value_db);
    lv_coord_t bar_h = lv_obj_get_height(ch->bar);
    lv_bar_set_value(ch->bar, pct, LV_ANIM_OFF);
    lv_coord_t ky = bar_h - (lv_coord_t)(pct * bar_h / 100) - KNOB_H / 2;
    lv_obj_set_y(ch->knob, ky);
    update_eq_label(ch);
}

/* ─────────────────────────────────────────────────────────── */
/*  Estilos                                                    */
/* ─────────────────────────────────────────────────────────── */
static void init_styles(void)
{
    lv_style_init(&sty_screen);
    lv_style_set_bg_color(&sty_screen, C_BG);
    lv_style_set_bg_opa(&sty_screen, LV_OPA_COVER);
    lv_style_set_border_width(&sty_screen, 0);
    lv_style_set_pad_all(&sty_screen, 0);

    lv_style_init(&sty_topbar);
    lv_style_set_bg_color(&sty_topbar, C_TOPBAR);
    lv_style_set_bg_opa(&sty_topbar, LV_OPA_COVER);
    lv_style_set_border_width(&sty_topbar, 0);
    lv_style_set_pad_all(&sty_topbar, 0);

    lv_style_init(&sty_nav_btn);
    lv_style_set_bg_opa(&sty_nav_btn, LV_OPA_0);
    lv_style_set_border_color(&sty_nav_btn, C_BORDER3);
    lv_style_set_border_width(&sty_nav_btn, 1);
    lv_style_set_radius(&sty_nav_btn, 8);
    lv_style_set_text_color(&sty_nav_btn, C_TEXT_MID);
    lv_style_set_text_font(&sty_nav_btn, &lv_font_montserrat_14);
    lv_style_set_pad_hor(&sty_nav_btn, 16);
    lv_style_set_pad_ver(&sty_nav_btn, 8);

    lv_style_init(&sty_nav_btn_pr);
    lv_style_set_bg_color(&sty_nav_btn_pr, C_SURFACE2);
    lv_style_set_bg_opa(&sty_nav_btn_pr, LV_OPA_COVER);
    lv_style_set_translate_y(&sty_nav_btn_pr, 1);

    lv_style_init(&sty_mute_btn);
    lv_style_set_bg_opa(&sty_mute_btn, LV_OPA_0);
    lv_style_set_border_color(&sty_mute_btn, C_RED_MUTE);
    lv_style_set_border_width(&sty_mute_btn, 1);
    lv_style_set_radius(&sty_mute_btn, 8);
    lv_style_set_text_color(&sty_mute_btn, C_RED_MUTE);
    lv_style_set_text_font(&sty_mute_btn, &lv_font_montserrat_14);
    lv_style_set_pad_hor(&sty_mute_btn, 16);
    lv_style_set_pad_ver(&sty_mute_btn, 8);

    lv_style_init(&sty_mute_btn_active);
    lv_style_set_bg_color(&sty_mute_btn_active, C_RED_MUTE);
    lv_style_set_bg_opa(&sty_mute_btn_active, LV_OPA_COVER);
    lv_style_set_text_color(&sty_mute_btn_active, lv_color_make(0xFF,0xFF,0xFF));

    lv_style_init(&sty_mute_btn_pr);
    lv_style_set_translate_y(&sty_mute_btn_pr, 1);

    lv_style_init(&sty_divider);
    lv_style_set_bg_color(&sty_divider, C_BORDER);
    lv_style_set_bg_opa(&sty_divider, LV_OPA_COVER);
    lv_style_set_border_width(&sty_divider, 0);
    lv_style_set_radius(&sty_divider, 0);

    lv_style_init(&sty_surface_box);
    lv_style_set_bg_color(&sty_surface_box, C_SURFACE);
    lv_style_set_bg_opa(&sty_surface_box, LV_OPA_COVER);
    lv_style_set_border_color(&sty_surface_box, C_BORDER);
    lv_style_set_border_width(&sty_surface_box, 1);
    lv_style_set_radius(&sty_surface_box, 8);
    lv_style_set_pad_all(&sty_surface_box, 10);

    lv_style_init(&sty_slider_bg);
    lv_style_set_bg_color(&sty_slider_bg, C_SURFACE2);
    lv_style_set_bg_opa(&sty_slider_bg, LV_OPA_COVER);
    lv_style_set_border_color(&sty_slider_bg, C_BORDER2);
    lv_style_set_border_width(&sty_slider_bg, 1);
    lv_style_set_radius(&sty_slider_bg, 4);

    lv_style_init(&sty_slider_ind);
    lv_style_set_bg_color(&sty_slider_ind, C_CYAN);
    lv_style_set_bg_opa(&sty_slider_ind, LV_OPA_COVER);
    lv_style_set_radius(&sty_slider_ind, 3);

    lv_style_init(&sty_knob);
    lv_style_set_bg_color(&sty_knob, C_KNOB);
    lv_style_set_bg_opa(&sty_knob, LV_OPA_COVER);
    lv_style_set_border_width(&sty_knob, 0);
    lv_style_set_radius(&sty_knob, 4);
    lv_style_set_shadow_width(&sty_knob, 8);
    lv_style_set_shadow_color(&sty_knob, lv_color_make(0x00,0x00,0x00));
    lv_style_set_shadow_opa(&sty_knob, LV_OPA_50);

    /* ── Label canal centrado ── */
    lv_style_init(&sty_ch_lbl);
    lv_style_set_text_color(&sty_ch_lbl, C_ACCENT);
    lv_style_set_text_font(&sty_ch_lbl, &lv_font_montserrat_14);
    lv_style_set_text_align(&sty_ch_lbl, LV_TEXT_ALIGN_CENTER);

    /* ── Label valor centrado ── */
    lv_style_init(&sty_value_lbl);
    lv_style_set_bg_color(&sty_value_lbl, C_VALUE_BG);
    lv_style_set_bg_opa(&sty_value_lbl, LV_OPA_COVER);
    lv_style_set_border_width(&sty_value_lbl, 0);
    lv_style_set_radius(&sty_value_lbl, 5);
    lv_style_set_text_color(&sty_value_lbl, C_CYAN);
    lv_style_set_text_font(&sty_value_lbl, &lv_font_montserrat_14);
    lv_style_set_text_align(&sty_value_lbl, LV_TEXT_ALIGN_CENTER);
    lv_style_set_pad_hor(&sty_value_lbl, 6);
    lv_style_set_pad_ver(&sty_value_lbl, 4);

    lv_style_init(&sty_scale_lbl);
    lv_style_set_text_color(&sty_scale_lbl, C_TEXT_SCALE);
    lv_style_set_text_font(&sty_scale_lbl, &lv_font_montserrat_14);

    /* ── Título sección centrado ── */
    lv_style_init(&sty_section_lbl);
    lv_style_set_text_color(&sty_section_lbl, C_TEXT_DIM);
    lv_style_set_text_font(&sty_section_lbl, &lv_font_montserrat_14);
    lv_style_set_text_align(&sty_section_lbl, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&sty_bar_green);
    lv_style_set_bg_color(&sty_bar_green, C_GREEN);
    lv_style_set_bg_opa(&sty_bar_green, LV_OPA_COVER);
    lv_style_set_border_width(&sty_bar_green, 0);
    lv_style_set_radius(&sty_bar_green, 2);

    lv_style_init(&sty_bar_yellow);
    lv_style_set_bg_color(&sty_bar_yellow, C_YELLOW);
    lv_style_set_bg_opa(&sty_bar_yellow, LV_OPA_COVER);
    lv_style_set_border_width(&sty_bar_yellow, 0);
    lv_style_set_radius(&sty_bar_yellow, 2);

    lv_style_init(&sty_bar_orange);
    lv_style_set_bg_color(&sty_bar_orange, C_ORANGE);
    lv_style_set_bg_opa(&sty_bar_orange, LV_OPA_COVER);
    lv_style_set_border_width(&sty_bar_orange, 0);
    lv_style_set_radius(&sty_bar_orange, 2);

    lv_style_init(&sty_vu_bg);
    lv_style_set_bg_color(&sty_vu_bg, C_SURFACE2);
    lv_style_set_bg_opa(&sty_vu_bg, LV_OPA_COVER);
    lv_style_set_border_width(&sty_vu_bg, 0);
    lv_style_set_radius(&sty_vu_bg, 3);

    lv_style_init(&sty_vu_fill);
    lv_style_set_bg_color(&sty_vu_fill, C_GREEN);
    lv_style_set_bg_opa(&sty_vu_fill, LV_OPA_COVER);
    lv_style_set_border_width(&sty_vu_fill, 0);
    lv_style_set_radius(&sty_vu_fill, 3);
}

/* ─────────────────────────────────────────────────────────── */
/*  Callback drag slider                                       */
/* ─────────────────────────────────────────────────────────── */
static void cb_slider_press(lv_event_t *e)
{
    eq_ch_t   *ch  = (eq_ch_t *)lv_event_get_user_data(e);
    lv_obj_t  *bar = lv_event_get_target(e);
    lv_indev_t *iv = lv_indev_get_act();
    lv_point_t  pt;
    lv_indev_get_point(iv, &pt);

    lv_coord_t abs_y = lv_obj_get_y(bar)
                     + lv_obj_get_y(lv_obj_get_parent(bar))
                     + TOPBAR_H + 1;
    lv_coord_t bar_h = lv_obj_get_height(bar);
    int32_t pct = 100 - (int32_t)((pt.y - abs_y) * 100 / bar_h);
    pct = LV_CLAMP(0, pct, 100);

    ch->value_db = pct_to_db(pct);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    lv_obj_set_y(ch->knob, bar_h - (lv_coord_t)(pct * bar_h / 100) - KNOB_H / 2);
    update_eq_label(ch);
    audio_apply_eq(ch);
}

/* ─────────────────────────────────────────────────────────── */
/*  Canal EQ — label + escala + track + valor, todos centrados */
/*                                                             */
/*  Cada canal ocupa CH_COL_W px de ancho. Dentro:            */
/*    inner_w = SCALE_W + 4 + SLIDER_W                        */
/*  Se centra con offset cx_off = (CH_COL_W - inner_w) / 2    */
/*                                                             */
/*  Label nombre  → ancho inner_w, text_align CENTER          */
/*  Escala         → alineada a la izquierda de inner_w       */
/*  Track          → a la derecha de la escala                 */
/*  Label valor   → ancho inner_w, text_align CENTER          */
/* ─────────────────────────────────────────────────────────── */
static void build_eq_channel(lv_obj_t  *parent,
                               eq_ch_t   *ch,
                               const char *name,
                               int16_t    default_db,
                               lv_coord_t col_x,
                               lv_coord_t col_y)
{
    ch->value_db = default_db;

    lv_coord_t inner_w = SCALE_W + 4 + SLIDER_W;
    lv_coord_t cx_off  = (CH_COL_W - inner_w) / 2;
    lv_coord_t origin  = col_x + cx_off;

    /* ── Nombre canal centrado ── */
    lv_obj_t *lname = lv_label_create(parent);
    lv_label_set_text(lname, name);
    lv_obj_add_style(lname, &sty_ch_lbl, 0);
    lv_obj_set_width(lname, inner_w);
    lv_obj_set_pos(lname, origin, col_y);

    lv_coord_t slider_y = col_y + 20;
    lv_coord_t track_x  = origin + SCALE_W + 4;

    /* ── Escala dB ── */
    const char *marks[] = {"+12","+6","0","-6","-12","-18","-24"};
    for (int i = 0; i < 7; i++) {
        lv_obj_t *m = lv_label_create(parent);
        lv_label_set_text(m, marks[i]);
        lv_obj_add_style(m, &sty_scale_lbl, 0);
        lv_coord_t my = slider_y + (lv_coord_t)(i * SLIDER_H / 6) - 5;
        lv_obj_set_pos(m, origin, my);
    }

    /* ── Track (lv_bar) ── */
    ch->bar = lv_bar_create(parent);
    lv_obj_set_size(ch->bar, SLIDER_W, SLIDER_H);
    lv_obj_set_pos(ch->bar, track_x, slider_y);
    lv_bar_set_range(ch->bar, 0, 100);
    lv_bar_set_value(ch->bar, db_to_pct(default_db), LV_ANIM_OFF);
    lv_obj_add_style(ch->bar, &sty_slider_bg, LV_PART_MAIN);
    lv_obj_add_style(ch->bar, &sty_slider_ind, LV_PART_INDICATOR);
    lv_obj_add_flag(ch->bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ch->bar, cb_slider_press, LV_EVENT_PRESSING, ch);

    /* ── Línea 0 dB ── */
    lv_obj_t *zl = lv_obj_create(parent);
    lv_obj_set_size(zl, SLIDER_W, 1);
    lv_obj_set_pos(zl, track_x, slider_y + SLIDER_H / 2);
    lv_obj_set_style_bg_color(zl, C_ZERO_LINE, 0);
    lv_obj_set_style_bg_opa(zl, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(zl, 0, 0);

    /* ── Knob ── */
    ch->knob = lv_obj_create(parent);
    lv_obj_set_size(ch->knob, SLIDER_W - 4, KNOB_H);
    int32_t pct = db_to_pct(default_db);
    lv_coord_t ky = slider_y + SLIDER_H
                  - (lv_coord_t)(pct * SLIDER_H / 100) - KNOB_H / 2;
    lv_obj_set_pos(ch->knob, track_x + 2, ky);
    lv_obj_add_style(ch->knob, &sty_knob, 0);
    lv_obj_clear_flag(ch->knob, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Valor dB centrado — mismo ancho y origen que el label ── */
    ch->lbl_val = lv_label_create(parent);
    lv_obj_add_style(ch->lbl_val, &sty_value_lbl, 0);
    lv_obj_set_width(ch->lbl_val, inner_w);
    lv_obj_set_pos(ch->lbl_val, origin, slider_y + SLIDER_H + 8);
    update_eq_label(ch);
}

/* ─────────────────────────────────────────────────────────── */
/*  Panel izquierdo — 3 columnas equidistantes                 */
/* ─────────────────────────────────────────────────────────── */
static void build_left_panel(lv_obj_t *parent)
{
    lv_coord_t y0 = TOPBAR_H + 2;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LEFT_W, CONTENT_H);
    lv_obj_set_pos(panel, 0, y0);
    lv_obj_set_style_bg_color(panel, C_BG, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t gap      = (LEFT_W - 3 * CH_COL_W) / 4;
    lv_coord_t x_master = gap;
    lv_coord_t x_bass   = x_master + CH_COL_W + gap;
    lv_coord_t x_treble = x_bass   + CH_COL_W + gap;
    lv_coord_t ch_y     = 12;

    build_eq_channel(panel, &ac.master, "MASTER", 0, x_master, ch_y);
    build_eq_channel(panel, &ac.bass,   "BASS",   0, x_bass,   ch_y);
    build_eq_channel(panel, &ac.treble, "TREBLE", 0, x_treble, ch_y);

    /* Separador derecho */
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, 1, CONTENT_H);
    lv_obj_set_pos(sep, LEFT_W, y0);
    lv_obj_set_style_bg_color(sep, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
}

/* ─────────────────────────────────────────────────────────── */
/*  Spectrum — título centrado sobre el gráfico                */
/* ─────────────────────────────────────────────────────────── */
static void build_spectrum(lv_obj_t *parent,
                             lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_add_style(box, &sty_surface_box, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    /* Título centrado — ocupa todo el ancho interior del box */
    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, "SPECTRUM");
    lv_obj_add_style(title, &sty_section_lbl, 0);
    lv_obj_set_width(title, w - 20);   /* w - 2*pad */
    lv_obj_set_pos(title, 0, 0);

    lv_coord_t bars_y = 22;
    lv_coord_t bars_h = h - bars_y - 28 - 20;
    lv_coord_t bar_w  = (w - 20 - (SPECTRUM_BARS - 1) * 2) / SPECTRUM_BARS;

    static const uint8_t init_pct[SPECTRUM_BARS] = {
        30,42,55,65,72,78,74,69,76,82,88,91,
        97,85,78,82,76,71,68,65,60,55,50,44
    };

    for (int i = 0; i < SPECTRUM_BARS; i++) {
        lv_coord_t bx       = i * (bar_w + 2);
        lv_coord_t bar_h_px = (lv_coord_t)(init_pct[i] * bars_h / 100);

        /* Pico */
        ac.spec_peaks[i] = lv_obj_create(box);
        lv_obj_set_size(ac.spec_peaks[i], bar_w, 2);
        lv_obj_set_pos(ac.spec_peaks[i], bx,
                       bars_y + bars_h - bar_h_px - 4);
        lv_obj_set_style_bg_color(ac.spec_peaks[i], C_GREEN_PEAK, 0);
        lv_obj_set_style_bg_opa(ac.spec_peaks[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ac.spec_peaks[i], 0, 0);
        lv_obj_set_style_radius(ac.spec_peaks[i], 1, 0);
        lv_obj_clear_flag(ac.spec_peaks[i], LV_OBJ_FLAG_SCROLLABLE);

        /* Barra */
        ac.spec_bars[i] = lv_obj_create(box);
        lv_obj_set_size(ac.spec_bars[i], bar_w, LV_MAX(3, bar_h_px));
        lv_obj_set_pos(ac.spec_bars[i], bx, bars_y + bars_h - bar_h_px);
        if      (i == 12) lv_obj_add_style(ac.spec_bars[i], &sty_bar_yellow, 0);
        else if (i == 13) lv_obj_add_style(ac.spec_bars[i], &sty_bar_orange, 0);
        else              lv_obj_add_style(ac.spec_bars[i], &sty_bar_green,  0);
        lv_obj_set_style_border_width(ac.spec_bars[i], 0, 0);
        lv_obj_clear_flag(ac.spec_bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Labels frecuencia */
    const char *flbls[] = {"20","100","500","2k","8k","20k"};
    int         fpos[]  = {0, 4, 9, 13, 18, 23};
    for (int i = 0; i < 6; i++) {
        lv_obj_t *fl = lv_label_create(box);
        lv_label_set_text(fl, flbls[i]);
        lv_obj_add_style(fl, &sty_scale_lbl, 0);
        lv_obj_set_pos(fl, fpos[i] * (bar_w + 2) - 2, bars_y + bars_h + 4);
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  VU Meters                                                  */
/* ─────────────────────────────────────────────────────────── */
static void build_vu_meters(lv_obj_t *parent,
                              lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, 52);
    lv_obj_set_pos(box, x, y);
    lv_obj_add_style(box, &sty_surface_box, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t tw = w - 50;

    /* Canal L */
    lv_obj_t *ll = lv_label_create(box);
    lv_label_set_text(ll, "L");
    lv_obj_add_style(ll, &sty_scale_lbl, 0);
    lv_obj_set_pos(ll, 0, 2);

    lv_obj_t *tl = lv_obj_create(box);
    lv_obj_set_size(tl, tw, VU_H);
    lv_obj_set_pos(tl, 16, 0);
    lv_obj_add_style(tl, &sty_vu_bg, 0);
    lv_obj_clear_flag(tl, LV_OBJ_FLAG_SCROLLABLE);

    ac.vu_l = lv_obj_create(tl);
    lv_obj_set_size(ac.vu_l, tw * 72 / 100, VU_H);
    lv_obj_set_pos(ac.vu_l, 0, 0);
    lv_obj_add_style(ac.vu_l, &sty_vu_fill, 0);
    lv_obj_clear_flag(ac.vu_l, LV_OBJ_FLAG_SCROLLABLE);

    ac.vu_peak_l = lv_obj_create(tl);
    lv_obj_set_size(ac.vu_peak_l, 3, VU_H);
    lv_obj_set_pos(ac.vu_peak_l, tw * 72 / 100 - 3, 0);
    lv_obj_set_style_bg_color(ac.vu_peak_l, C_RED, 0);
    lv_obj_set_style_bg_opa(ac.vu_peak_l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ac.vu_peak_l, 0, 0);
    lv_obj_set_style_radius(ac.vu_peak_l, 0, 0);
    lv_obj_clear_flag(ac.vu_peak_l, LV_OBJ_FLAG_SCROLLABLE);

    ac.lbl_vu_l = lv_label_create(box);
    lv_label_set_text(ac.lbl_vu_l, "-3");
    lv_obj_add_style(ac.lbl_vu_l, &sty_scale_lbl, 0);
    lv_obj_set_pos(ac.lbl_vu_l, w - 28, 2);

    /* Canal R */
    lv_obj_t *lr = lv_label_create(box);
    lv_label_set_text(lr, "R");
    lv_obj_add_style(lr, &sty_scale_lbl, 0);
    lv_obj_set_pos(lr, 0, VU_H + 12);

    lv_obj_t *tr = lv_obj_create(box);
    lv_obj_set_size(tr, tw, VU_H);
    lv_obj_set_pos(tr, 16, VU_H + 10);
    lv_obj_add_style(tr, &sty_vu_bg, 0);
    lv_obj_clear_flag(tr, LV_OBJ_FLAG_SCROLLABLE);

    ac.vu_r = lv_obj_create(tr);
    lv_obj_set_size(ac.vu_r, tw * 65 / 100, VU_H);
    lv_obj_set_pos(ac.vu_r, 0, 0);
    lv_obj_add_style(ac.vu_r, &sty_vu_fill, 0);
    lv_obj_clear_flag(ac.vu_r, LV_OBJ_FLAG_SCROLLABLE);

    ac.vu_peak_r = lv_obj_create(tr);
    lv_obj_set_size(ac.vu_peak_r, 3, VU_H);
    lv_obj_set_pos(ac.vu_peak_r, tw * 65 / 100 - 3, 0);
    lv_obj_set_style_bg_color(ac.vu_peak_r, C_RED, 0);
    lv_obj_set_style_bg_opa(ac.vu_peak_r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ac.vu_peak_r, 0, 0);
    lv_obj_set_style_radius(ac.vu_peak_r, 0, 0);
    lv_obj_clear_flag(ac.vu_peak_r, LV_OBJ_FLAG_SCROLLABLE);

    ac.lbl_vu_r = lv_label_create(box);
    lv_label_set_text(ac.lbl_vu_r, "-5");
    lv_obj_add_style(ac.lbl_vu_r, &sty_scale_lbl, 0);
    lv_obj_set_pos(ac.lbl_vu_r, w - 28, VU_H + 12);
}

/* ─────────────────────────────────────────────────────────── */
/*  Topbar                                                     */
/* ─────────────────────────────────────────────────────────── */
static void cb_back(lv_event_t *e) { (void)e; if (s_back_cb) s_back_cb(e); }
static void cb_mute(lv_event_t *e)
{
    (void)e;
    ac.muted = !ac.muted;
    if (ac.muted) lv_obj_add_style(ac.btn_mute, &sty_mute_btn_active, 0);
    else          lv_obj_remove_style(ac.btn_mute, &sty_mute_btn_active, 0);
    lv_obj_invalidate(ac.btn_mute);
    audio_apply_mute(ac.muted);
}

static void build_topbar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, TOPBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_add_style(bar, &sty_topbar, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, SCR_W, 1);
    lv_obj_set_pos(line, 0, TOPBAR_H);
    lv_obj_add_style(line, &sty_divider, 0);

    lv_obj_t *btn_back = lv_btn_create(bar);
    lv_obj_add_style(btn_back, &sty_nav_btn, 0);
    lv_obj_add_style(btn_back, &sty_nav_btn_pr, LV_STATE_PRESSED);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_add_event_cb(btn_back, cb_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn_back);
    lv_label_set_text(lb, LV_SYMBOL_LEFT " Musica");
    lv_obj_center(lb);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_AUDIO " Audio Console");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    ac.btn_mute = lv_btn_create(bar);
    lv_obj_add_style(ac.btn_mute, &sty_mute_btn, 0);
    lv_obj_add_style(ac.btn_mute, &sty_mute_btn_pr, LV_STATE_PRESSED);
    lv_obj_align(ac.btn_mute, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(ac.btn_mute, cb_mute, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lm = lv_label_create(ac.btn_mute);
    lv_label_set_text(lm, "MUTE");
    lv_obj_center(lm);
}

/* ─────────────────────────────────────────────────────────── */
/*  Panel derecho                                              */
/* ─────────────────────────────────────────────────────────── */
static void build_right_panel(lv_obj_t *parent)
{
    lv_coord_t x0  = LEFT_W + 2;
    lv_coord_t y0  = TOPBAR_H + 2;
    lv_coord_t pad = 12;
    lv_coord_t spec_h = CONTENT_H - 52 - pad * 3;

    build_spectrum(parent, x0 + pad, y0 + pad,
                   RIGHT_W - pad * 2, spec_h);
    build_vu_meters(parent, x0 + pad,
                    y0 + pad + spec_h + pad,
                    RIGHT_W - pad * 2);
}

/* ─────────────────────────────────────────────────────────── */
/*  API PÚBLICA                                                */
/* ─────────────────────────────────────────────────────────── */

static void ac_anim_cb(lv_timer_t *t); /* forward */
static void audio_console_build(lv_obj_t *parent)
{
    memset(&ac, 0, sizeof(ac));
    init_styles();
    lv_obj_add_style(parent, &sty_screen, 0);
    build_topbar(parent);
    build_left_panel(parent);
    build_right_panel(parent);
    s_anim_timer = lv_timer_create(ac_anim_cb, 80, NULL);
}

void audio_console_set_eq(int16_t master_db, int16_t bass_db, int16_t treble_db)
{
    ac.master.value_db = LV_CLAMP(-12, master_db, 12);
    ac.bass.value_db   = LV_CLAMP(-12, bass_db,   12);
    ac.treble.value_db = LV_CLAMP(-12, treble_db, 12);
    update_eq_visual(&ac.master);
    update_eq_visual(&ac.bass);
    update_eq_visual(&ac.treble);
}

void audio_console_set_spectrum(float bars[], int n)
{
    int count = LV_MIN(n, SPECTRUM_BARS);
    for (int i = 0; i < count; i++) {
        float v = LV_CLAMP(0.0f, bars[i], 1.0f);
        if (ac.muted) v = 0.0f;
        lv_coord_t h = LV_MAX(3, (lv_coord_t)(v * 150));
        lv_obj_set_height(ac.spec_bars[i], h);
        lv_coord_t by = lv_obj_get_y(ac.spec_bars[i]);
        lv_obj_set_y(ac.spec_peaks[i], by - 4);
    }
}

void audio_console_set_vu(float l, float r)
{
    if (ac.muted) { l = 0.0f; r = 0.0f; }
    lv_coord_t tw = lv_obj_get_width(lv_obj_get_parent(ac.vu_l));
    lv_coord_t lw = LV_MAX(1, (lv_coord_t)(l * tw));
    lv_coord_t rw = LV_MAX(1, (lv_coord_t)(r * tw));
    lv_obj_set_width(ac.vu_l, lw);
    lv_obj_set_width(ac.vu_r, rw);
    lv_obj_set_x(ac.vu_peak_l, LV_MAX(0, lw - 3));
    lv_obj_set_x(ac.vu_peak_r, LV_MAX(0, rw - 3));
    char buf[8];
    int ldb = (l > 0.01f) ? (int)(20.0f * log10f(l)) : -60;
    int rdb = (r > 0.01f) ? (int)(20.0f * log10f(r)) : -60;
    snprintf(buf, sizeof(buf), "%d", ldb); lv_label_set_text(ac.lbl_vu_l, buf);
    snprintf(buf, sizeof(buf), "%d", rdb); lv_label_set_text(ac.lbl_vu_r, buf);
}

void audio_console_set_mute(bool muted)
{
    if (ac.muted == muted) return;
    ac.muted = muted;
    if (muted) lv_obj_add_style(ac.btn_mute, &sty_mute_btn_active, 0);
    else       lv_obj_remove_style(ac.btn_mute, &sty_mute_btn_active, 0);
    lv_obj_invalidate(ac.btn_mute);
    audio_apply_mute(ac.muted);
}

bool    audio_console_get_mute(void)   { return ac.muted;           }
int16_t audio_console_get_master(void) { return ac.master.value_db; }
int16_t audio_console_get_bass(void)   { return ac.bass.value_db;   }
int16_t audio_console_get_treble(void) { return ac.treble.value_db; }

/* ── Implementación audio_apply_eq ──────────────────────── */
static void audio_apply_eq(void *ch_ptr)
{
    eq_ch_t *ch = (eq_ch_t *)ch_ptr;
    if (ch == &ac.master) {
        int vol = (int)((ch->value_db + 12) * 100 / 24);
        s_vol_pct = vol;
        ha_media_set_volume("media_player.spotify_premiun", (float)vol / 100.0f);
        char path[32];
        snprintf(path, sizeof(path), "/api/volume?vol=%d", vol);
        volumio_get_path(path);
        ESP_LOGI(TAG, "MASTER: %ddB (%d%%)", ch->value_db, vol);
    } else if (ch == &ac.bass) {
        volumio_eq_bass(ch->value_db);
        ESP_LOGI(TAG, "BASS: %ddB", ch->value_db);
    } else if (ch == &ac.treble) {
        volumio_eq_treble(ch->value_db);
        ESP_LOGI(TAG, "TREBLE: %ddB", ch->value_db);
    }
}

/* ── Timer de animación ─────────────────────────────────── */
static void ac_anim_cb(lv_timer_t *t)
{
    (void)t;
    int maxh = 150;
    for (int i = 0; i < SPECTRUM_BARS; i++) {
        float ff = 0.5f + 0.8f * sinf((float)i / (float)SPECTRUM_BARS * 3.14159f);
        int target = (int)(8 + ff * (rand() % 120) * s_vol_pct / 100.0f);
        if (target > maxh) target = maxh;
        s_spec_h[i] = (s_spec_h[i] * 4 + target) / 5;
        if (s_spec_h[i] > s_spec_peak[i]) {
            s_spec_peak[i] = s_spec_h[i];
            s_spec_hold[i] = 15;
        } else {
            if (s_spec_hold[i] > 0) s_spec_hold[i]--;
            else if (s_spec_peak[i] > 0) s_spec_peak[i] -= 2;
        }
        if (!ac.spec_bars[i] || !ac.spec_peaks[i]) continue;
        bsp_display_lock(0);
        lv_obj_set_height(ac.spec_bars[i], LV_MAX(3, s_spec_h[i]));
        lv_coord_t by = lv_obj_get_y(ac.spec_bars[i]);
        lv_obj_set_y(ac.spec_peaks[i], by - 4);
        bsp_display_unlock();
    }
    if (!ac.vu_l || !ac.vu_r) return;
    lv_coord_t tw = lv_obj_get_width(lv_obj_get_parent(ac.vu_l));
    float lf = (float)(rand() % 80 + 20) * s_vol_pct / 10000.0f;
    float rf = (float)(rand() % 80 + 20) * s_vol_pct / 10000.0f;
    lv_coord_t lw = LV_MAX(1,(lv_coord_t)(lf * tw));
    lv_coord_t rw = LV_MAX(1,(lv_coord_t)(rf * tw));
    char buf[8];
    bsp_display_lock(0);
    lv_obj_set_width(ac.vu_l, lw);
    lv_obj_set_width(ac.vu_r, rw);
    lv_obj_set_x(ac.vu_peak_l, LV_MAX(0, lw - 3));
    lv_obj_set_x(ac.vu_peak_r, LV_MAX(0, rw - 3));
    int ldb = (lf > 0.01f) ? (int)(20.0f * log10f(lf)) : -60;
    int rdb = (rf > 0.01f) ? (int)(20.0f * log10f(rf)) : -60;
    snprintf(buf, sizeof(buf), "%d", ldb); lv_label_set_text(ac.lbl_vu_l, buf);
    snprintf(buf, sizeof(buf), "%d", rdb); lv_label_set_text(ac.lbl_vu_r, buf);
    bsp_display_unlock();
}

/* ══ API PÚBLICA DEL PROYECTO ════════════════════════════ */
lv_obj_t *screen_audio_create(lv_event_cb_t back_cb)
{
    s_back_cb = back_cb;
    memset(&ac, 0, sizeof(ac));
    for (int i = 0; i < SPECTRUM_BARS; i++) s_spec_h[i] = 20 + rand() % 60;
    s_screen = lv_obj_create(NULL);
    init_styles();
    lv_obj_add_style(s_screen, &sty_screen, 0);
    audio_console_build(s_screen);
    return s_screen;
}

lv_obj_t *screen_audio_get_screen(void)         { return s_screen; }

void screen_audio_set_volume(int vol)
{
    s_vol_pct = vol;
    if (!ac.master.bar) return;
    ac.master.value_db = LV_CLAMP(-12, (int16_t)(vol * 24 / 100 - 12), 12);
    bsp_display_lock(0);
    update_eq_visual(&ac.master);
    bsp_display_unlock();
}

void screen_audio_update_eq(void)               { }
void screen_audio_set_back_cb(lv_event_cb_t cb) { s_back_cb = cb; }

