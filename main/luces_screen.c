/**
 * @file luces_screen.c
 * @brief Pantalla Luces de la Oficina - segun mockup luces_oficina_pro
 * LVGL v8 — 1024x600
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "lvgl.h"
#include "esp_log.h"
#include "luces_screen.h"
#include "app_nav.h"

static const char *TAG = "luces";

/* ── Dimensiones ─────────────────────────────────────────── */
#define SCR_W    1024
#define SCR_H    600
#define TOP_H    44
#define PAD      14
#define GAP_C    10   /* gap entre cards */
#define GAP_R    10
#define COLS     3
#define ROWS     2
#define BOT_H    40
#define CARD_W   ((SCR_W - PAD*2 - GAP_C*(COLS-1)) / COLS)  /* ~325 */
#define CARD_H   ((SCR_H - TOP_H - PAD*2 - GAP_R*(ROWS-1) - BOT_H - GAP_C) / ROWS) /* ~98 */
#define ICON_BOX 56
#define TGL_W    52
#define TGL_H    28
#define TGL_DOT  20

/* ── Colores ─────────────────────────────────────────────── */
#define C_BG         lv_color_hex(0x0A0E18)
#define C_TOP        lv_color_hex(0x141A28)
#define C_CARD_OFF   lv_color_hex(0x1B2233)   /* card apagada: gris azulado claro */
#define C_CARD_ON    lv_color_hex(0x16301A)   /* card encendida: verde */
#define C_BDR_OFF    lv_color_hex(0x323C58)   /* borde mas visible */
#define C_BDR_ON     lv_color_hex(0x55963A)
#define C_ICON_OFF   lv_color_hex(0x7E88AC)   /* icono apagado mas claro */
#define C_ICON_ON    lv_color_hex(0xA6F03C)
#define C_WRAP_OFF   lv_color_hex(0x2A3247)
#define C_WRAP_ON    lv_color_hex(0x2E5418)
#define C_NAME_OFF   lv_color_hex(0xFFFFFF)   /* nombre BLANCO = alto contraste */
#define C_NAME_ON    lv_color_hex(0xC0F560)
#define C_STAT_OFF   lv_color_hex(0x99A2C2)   /* estado gris legible */
#define C_STAT_ON    lv_color_hex(0x9AD840)
#define C_TGL_OFF    lv_color_hex(0x323C58)
#define C_TGL_ON     lv_color_hex(0x3E7320)
#define C_DOT_OFF    lv_color_hex(0xB6BEDA)   /* dot apagado claro = visible */
#define C_DOT_ON     lv_color_hex(0xC0F560)
#define C_GLOW_ON    lv_color_hex(0x6AB030)
#define C_GOLD       lv_color_hex(0xF5C842)   /* titulo dorado brillante */
#define C_DIM        lv_color_hex(0x99A2C2)
#define C_TITLE_LINE lv_color_hex(0x323C58)

/* ── Luces ───────────────────────────────────────────────── */
#define LIGHT_COUNT 6
static const char *LIGHT_NAMES[LIGHT_COUNT] = {
    "Tubo de LED", "Dicroicas + Escrit.", "Luz de Arriba",
    "Tira de LED", "Luz de Abajo",  "Luz del Medio"
};

/* ── Estado ──────────────────────────────────────────────── */
typedef struct {
    lv_obj_t *card;
    lv_obj_t *icon_wrap;
    lv_obj_t *icon_lbl;
    lv_obj_t *name_lbl;
    lv_obj_t *stat_lbl;
    lv_obj_t *tgl_track;
    lv_obj_t *tgl_dot;
    lv_obj_t *glow_bar;
    bool state;
} light_widget_t;

static struct {
    light_widget_t lights[LIGHT_COUNT];
    lv_obj_t *lbl_count;
    bool s_styles_init;
} ls;

/* ── Estilos ─────────────────────────────────────────────── */
static lv_style_t s_scr, s_top, s_line;
static lv_style_t s_card_base;
static lv_style_t s_btn_on, s_btn_off;

static void init_styles(void)
{
    if (ls.s_styles_init) return;
    ls.s_styles_init = true;

    lv_style_init(&s_scr);
    lv_style_set_bg_color(&s_scr, C_BG);
    lv_style_set_bg_opa(&s_scr, LV_OPA_COVER);
    lv_style_set_pad_all(&s_scr, 0);
    lv_style_set_border_width(&s_scr, 0);

    lv_style_init(&s_top);
    lv_style_set_bg_color(&s_top, C_TOP);
    lv_style_set_bg_opa(&s_top, LV_OPA_COVER);
    lv_style_set_pad_all(&s_top, 0);
    lv_style_set_border_width(&s_top, 0);
    lv_style_set_radius(&s_top, 0);
    lv_style_set_shadow_width(&s_top, 0);

    lv_style_init(&s_line);
    lv_style_set_bg_color(&s_line, C_TITLE_LINE);
    lv_style_set_bg_opa(&s_line, LV_OPA_COVER);
    lv_style_set_border_width(&s_line, 0);
    lv_style_set_radius(&s_line, 0);
    lv_style_set_shadow_width(&s_line, 0);

    lv_style_init(&s_card_base);
    lv_style_set_pad_all(&s_card_base, 0);
    lv_style_set_radius(&s_card_base, 14);
    lv_style_set_shadow_width(&s_card_base, 0);
    lv_style_set_border_width(&s_card_base, 2);

    lv_style_init(&s_btn_on);
    lv_style_set_bg_color(&s_btn_on, lv_color_hex(0x1A2A0A));
    lv_style_set_bg_opa(&s_btn_on, LV_OPA_COVER);
    lv_style_set_border_color(&s_btn_on, lv_color_hex(0x55963A));
    lv_style_set_border_width(&s_btn_on, 2);
    lv_style_set_radius(&s_btn_on, 10);
    lv_style_set_shadow_width(&s_btn_on, 0);
    lv_style_set_text_color(&s_btn_on, lv_color_hex(0xA6F03C));
    lv_style_set_text_font(&s_btn_on, &lv_font_montserrat_18);
    lv_style_set_pad_hor(&s_btn_on, 12);

    lv_style_init(&s_btn_off);
    lv_style_set_bg_color(&s_btn_off, lv_color_hex(0x2A0A0A));
    lv_style_set_bg_opa(&s_btn_off, LV_OPA_COVER);
    lv_style_set_border_color(&s_btn_off, lv_color_hex(0x8A2A2A));
    lv_style_set_border_width(&s_btn_off, 2);
    lv_style_set_radius(&s_btn_off, 10);
    lv_style_set_shadow_width(&s_btn_off, 0);
    lv_style_set_text_color(&s_btn_off, lv_color_hex(0xF06A6A));
    lv_style_set_text_font(&s_btn_off, &lv_font_montserrat_18);
    lv_style_set_pad_hor(&s_btn_off, 12);
}

/* ── Weak callbacks ──────────────────────────────────────── */
__attribute__((weak)) void on_luces_back(void) { app_nav_goto(SCREEN_MAIN_MENU); }
__attribute__((weak)) void on_luz_changed(int idx, bool state)
    { ESP_LOGI(TAG, "Luz %d: %s", idx, state?"ON":"OFF"); }

/* ── Update status count ─────────────────────────────────── */
static void update_count(void)
{
    if (!ls.lbl_count) return;
    int on = 0;
    for (int i = 0; i < LIGHT_COUNT; i++) if (ls.lights[i].state) on++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", on, LIGHT_COUNT);
    lv_label_set_text(ls.lbl_count, buf);
}

/* ── Apply visual state to a card ───────────────────────── */
static void apply_state(int idx)
{
    light_widget_t *w = &ls.lights[idx];
    bool on = w->state;

    lv_obj_set_style_bg_color(w->card, on ? C_CARD_ON : C_CARD_OFF, 0);
    lv_obj_set_style_border_color(w->card, on ? C_BDR_ON : C_BDR_OFF, 0);

    lv_obj_set_style_bg_color(w->icon_wrap, on ? C_WRAP_ON : C_WRAP_OFF, 0);
    lv_obj_set_style_border_color(w->icon_wrap, on ? lv_color_hex(0x4A7A20) : lv_color_hex(0x2A2A4A), 0);
    lv_obj_set_style_text_color(w->icon_lbl, on ? C_ICON_ON : C_ICON_OFF, 0);

    lv_obj_set_style_text_color(w->name_lbl, on ? C_NAME_ON : C_NAME_OFF, 0);

    lv_label_set_text(w->stat_lbl, on ? "Encendida" : "Apagada");
    lv_obj_set_style_text_color(w->stat_lbl, on ? C_STAT_ON : C_STAT_OFF, 0);

    lv_obj_set_style_bg_color(w->tgl_track, on ? C_TGL_ON : C_TGL_OFF, 0);
    lv_obj_set_style_border_color(w->tgl_track, on ? lv_color_hex(0x4A7A20) : lv_color_hex(0x2A2A4A), 0);
    lv_obj_set_style_bg_color(w->tgl_dot, on ? C_DOT_ON : C_DOT_OFF, 0);
    /* Dot position: left=OFF, right=ON */
    lv_obj_set_x(w->tgl_dot, on ? TGL_W - TGL_DOT - 3 : 3);

    lv_obj_set_style_bg_color(w->glow_bar, on ? C_GLOW_ON : lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(w->glow_bar, on ? LV_OPA_COVER : LV_OPA_0, 0);
}

/* ── Card click callback ─────────────────────────────────── */
static void cb_card(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ls.lights[idx].state = !ls.lights[idx].state;
    apply_state(idx);
    update_count();
    on_luz_changed(idx, ls.lights[idx].state);
}

static void cb_back(lv_event_t *e) { (void)e; on_luces_back(); }

static void cb_all_on(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < LIGHT_COUNT; i++) {
        ls.lights[i].state = true;
        apply_state(i);
        on_luz_changed(i, true);
    }
    update_count();
}

static void cb_all_off(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < LIGHT_COUNT; i++) {
        ls.lights[i].state = false;
        apply_state(i);
        on_luz_changed(i, false);
    }
    update_count();
}

/* ── Build one light card ────────────────────────────────── */
static void build_card(lv_obj_t *parent, int idx)
{
    int col = idx % COLS;
    int row = idx / COLS;
    int cx = PAD + col * (CARD_W + GAP_C);
    int cy = TOP_H + 1 + PAD + row * (CARD_H + GAP_R);

    light_widget_t *w = &ls.lights[idx];

    /* Card */
    w->card = lv_obj_create(parent);
    lv_obj_set_size(w->card, CARD_W, CARD_H);
    lv_obj_set_pos(w->card, cx, cy);
    lv_obj_add_style(w->card, &s_card_base, 0);
    lv_obj_set_style_bg_color(w->card, C_CARD_OFF, 0);
    lv_obj_set_style_bg_opa(w->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(w->card, C_BDR_OFF, 0);
    lv_obj_add_flag(w->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(w->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(w->card, cb_card, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    /* Icon wrap box */
    w->icon_wrap = lv_obj_create(w->card);
    lv_obj_set_size(w->icon_wrap, ICON_BOX, ICON_BOX);
    lv_obj_set_pos(w->icon_wrap, 14, (CARD_H - ICON_BOX) / 2);
    lv_obj_set_style_bg_color(w->icon_wrap, C_WRAP_OFF, 0);
    lv_obj_set_style_bg_opa(w->icon_wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(w->icon_wrap, lv_color_hex(0x2A2A4A), 0);
    lv_obj_set_style_border_width(w->icon_wrap, 1, 0);
    lv_obj_set_style_radius(w->icon_wrap, 12, 0);
    lv_obj_set_style_shadow_width(w->icon_wrap, 0, 0);
    lv_obj_clear_flag(w->icon_wrap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Icon label */
    w->icon_lbl = lv_label_create(w->icon_wrap);
    lv_label_set_text(w->icon_lbl, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(w->icon_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(w->icon_lbl, C_ICON_OFF, 0);
    lv_obj_center(w->icon_lbl);

    /* Name */
    w->name_lbl = lv_label_create(w->card);
    lv_label_set_text(w->name_lbl, LIGHT_NAMES[idx]);
    lv_obj_set_style_text_font(w->name_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(w->name_lbl, C_NAME_OFF, 0);
    lv_label_set_long_mode(w->name_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(w->name_lbl, CARD_W - ICON_BOX - TGL_W - 56);
    lv_obj_set_pos(w->name_lbl, ICON_BOX + 30, CARD_H/2 - 24);

    /* Status */
    w->stat_lbl = lv_label_create(w->card);
    lv_label_set_text(w->stat_lbl, "Apagada");
    lv_obj_set_style_text_font(w->stat_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(w->stat_lbl, C_STAT_OFF, 0);
    lv_obj_set_pos(w->stat_lbl, ICON_BOX + 30, CARD_H/2 + 6);

    /* Toggle track */
    w->tgl_track = lv_obj_create(w->card);
    lv_obj_set_size(w->tgl_track, TGL_W, TGL_H);
    lv_obj_set_pos(w->tgl_track, CARD_W - TGL_W - 14, (CARD_H - TGL_H) / 2);
    lv_obj_set_style_bg_color(w->tgl_track, C_TGL_OFF, 0);
    lv_obj_set_style_bg_opa(w->tgl_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(w->tgl_track, lv_color_hex(0x2A2A4A), 0);
    lv_obj_set_style_border_width(w->tgl_track, 1, 0);
    lv_obj_set_style_radius(w->tgl_track, TGL_H/2, 0);
    lv_obj_set_style_shadow_width(w->tgl_track, 0, 0);
    lv_obj_clear_flag(w->tgl_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Toggle dot */
    w->tgl_dot = lv_obj_create(w->tgl_track);
    lv_obj_set_size(w->tgl_dot, TGL_DOT, TGL_DOT);
    lv_obj_set_pos(w->tgl_dot, 3, (TGL_H - TGL_DOT) / 2);
    lv_obj_set_style_bg_color(w->tgl_dot, C_DOT_OFF, 0);
    lv_obj_set_style_bg_opa(w->tgl_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(w->tgl_dot, 0, 0);
    lv_obj_set_style_radius(w->tgl_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(w->tgl_dot, 0, 0);
    lv_obj_clear_flag(w->tgl_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Glow bar bottom */
    w->glow_bar = lv_obj_create(w->card);
    lv_obj_set_size(w->glow_bar, CARD_W - 40, 3);
    lv_obj_set_pos(w->glow_bar, 20, CARD_H - 8);
    lv_obj_set_style_bg_color(w->glow_bar, C_GLOW_ON, 0);
    lv_obj_set_style_bg_opa(w->glow_bar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(w->glow_bar, 0, 0);
    lv_obj_set_style_radius(w->glow_bar, 2, 0);
    lv_obj_set_style_shadow_width(w->glow_bar, 0, 0);
    lv_obj_clear_flag(w->glow_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

/* ── Screen creation ─────────────────────────────────────── */
void luces_screen_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creando pantalla luces");
    memset(&ls, 0, sizeof(ls));
    init_styles();

    lv_obj_add_style(parent, &s_scr, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Topbar ── */
    lv_obj_t *top = lv_obj_create(parent);
    lv_obj_set_size(top, SCR_W, TOP_H);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_add_style(top, &s_top, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    /* Boton Atras */
    lv_obj_t *btn_back = lv_btn_create(top);
    lv_obj_set_size(btn_back, 110, 30);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_0, 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x4A5478), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 8, 0);
    lv_obj_set_style_shadow_width(btn_back, 0, 0);
    lv_obj_add_event_cb(btn_back, cb_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Atras");
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0xC8CEE6), 0);
    lv_obj_center(lbl_back);

    /* Titulo */
    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, LV_SYMBOL_CHARGE " Luces de la Oficina");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, C_GOLD, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Status count */
    lv_obj_t *st_lbl = lv_label_create(top);
    lv_label_set_text(st_lbl, "Encendidas:");
    lv_obj_set_style_text_font(st_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st_lbl, C_DIM, 0);
    lv_obj_align(st_lbl, LV_ALIGN_RIGHT_MID, -95, 0);

    ls.lbl_count = lv_label_create(top);
    lv_label_set_text(ls.lbl_count, "0 / 6");
    lv_obj_set_style_text_font(ls.lbl_count, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ls.lbl_count, lv_color_hex(0xA6F03C), 0);
    lv_obj_align(ls.lbl_count, LV_ALIGN_RIGHT_MID, -12, 0);

    /* Linea inferior topbar */
    lv_obj_t *ln = lv_obj_create(parent);
    lv_obj_set_size(ln, SCR_W, 1);
    lv_obj_set_pos(ln, 0, TOP_H);
    lv_obj_add_style(ln, &s_line, 0);

    /* ── Cards ── */
    for (int i = 0; i < LIGHT_COUNT; i++) build_card(parent, i);

    /* ── Bottom row ── */
    int bot_y = TOP_H + 1 + PAD + ROWS * CARD_H + (ROWS-1) * GAP_R + GAP_C;

    lv_obj_t *btn_on = lv_btn_create(parent);
    lv_obj_set_size(btn_on, (SCR_W - PAD*2 - GAP_C) / 2, BOT_H);
    lv_obj_set_pos(btn_on, PAD, bot_y);
    lv_obj_add_style(btn_on, &s_btn_on, 0);
    lv_obj_set_style_shadow_width(btn_on, 0, 0);
    lv_obj_add_event_cb(btn_on, cb_all_on, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_on = lv_label_create(btn_on);
    lv_label_set_text(lbl_on, LV_SYMBOL_CHARGE " Encender Todo");
    lv_obj_center(lbl_on);

    lv_obj_t *btn_off = lv_btn_create(parent);
    lv_obj_set_size(btn_off, (SCR_W - PAD*2 - GAP_C) / 2, BOT_H);
    lv_obj_set_pos(btn_off, PAD + (SCR_W - PAD*2 - GAP_C)/2 + GAP_C, bot_y);
    lv_obj_add_style(btn_off, &s_btn_off, 0);
    lv_obj_set_style_shadow_width(btn_off, 0, 0);
    lv_obj_add_event_cb(btn_off, cb_all_off, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_off = lv_label_create(btn_off);
    lv_label_set_text(lbl_off, LV_SYMBOL_CLOSE " Apagar Todo");
    lv_obj_center(lbl_off);

    ESP_LOGI(TAG, "Pantalla luces creada");
}

/* ── API publica ─────────────────────────────────────────── */
void luces_screen_set_light(int idx, bool state)
{
    if (idx < 0 || idx >= LIGHT_COUNT) return;
    ls.lights[idx].state = state;
    apply_state(idx);
    update_count();
}

bool luces_screen_get_state(int idx)
{
    if (idx < 0 || idx >= LIGHT_COUNT) return false;
    return ls.lights[idx].state;
}
