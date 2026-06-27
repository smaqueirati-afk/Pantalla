#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_check.h"
/**
 * @file screen_now_playing.c
 * @brief Now Playing screen â€” "Liquid Glass" dark aesthetic.
 *
 * Layout (1024Ã—600):
 *   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
 *   â”‚  [WiFi]  [BT]   [time/date]              [quality badge]â”‚  status bar 36px
 *   â”œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¤
 *   â”‚                                â”‚  Title (48px bold)     â”‚
 *   â”‚    Album Art 380Ã—380           â”‚  Artist (28px light)   â”‚
 *   â”‚    (rounded 24px, vinyl anim)  â”‚  Album (18px muted)    â”‚
 *   â”‚                                â”‚  â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€     â”‚
 *   â”‚                                â”‚  Progress bar          â”‚
 *   â”‚                                â”‚  Time elapsed / total  â”‚
 *   â”‚                                â”‚  â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€     â”‚
 *   â”‚                                â”‚  [prev] [play] [next]  â”‚
 *   â”‚                                â”‚  [vol knob] [mode btn] â”‚
 *   â”œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”´â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¤
 *   â”‚  FFT visualiser â€” 32 bars â€” full width                  â”‚  80px
 *   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
 */

#include "ui/screen_now_playing.h"
#include "ui/widget_fft.h"
#include "ui/widget_vinyl.h"
#include "ui/widget_audio_mode.h"
#include "state/app_state.h"
#include "state/event_bus.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *TAG = "screen_now_playing";

/* â”€â”€ Layout constants â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
#define SCR_W           1024
#define SCR_H           600
#define STATUS_H        36
#define FFT_H           80
#define ART_W           380
#define ART_H           380
#define ART_X           20
#define ART_Y           (STATUS_H + (SCR_H - STATUS_H - FFT_H - ART_H) / 2)
#define INFO_X          (ART_X + ART_W + 24)
#define INFO_W          (SCR_W - INFO_X - 16)
#define CTRL_Y          (STATUS_H + 20)

/* â”€â”€ Colours (defaults â€” overridden by palette events) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
#define COL_BG lv_color_make(0x10, 0x10, 0x14)
#define COL_SURFACE     lv_color_make(0x1A, 0x1A, 0x20)
#define COL_TEXT        lv_color_make(0xFF, 0xFF, 0xFF)
#define COL_TEXT2       lv_color_make(0xB3, 0xB3, 0xB3)
#define COL_ACCENT      lv_color_make(0x1D, 0xB9, 0x54)
#define COL_CTRL        lv_color_make(0x2A, 0x2A, 0x30)

/* â”€â”€ Widget references â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
static lv_obj_t *s_screen       = NULL;

/* Status bar */
static lv_obj_t *s_lbl_time     = NULL;
static lv_obj_t *s_lbl_wifi     = NULL;
static lv_obj_t *s_lbl_quality  = NULL;

/* Album art area */
static lv_obj_t *s_art_cont     = NULL;   /* rounded container */
static lv_obj_t *s_art_img      = NULL;   /* lv_img â€” album art */
static lv_obj_t *s_art_overlay  = NULL;   /* frosted glass tint */
static lv_img_dsc_t s_art_dsc = {0};

/* Track info */
static lv_obj_t *s_lbl_title    = NULL;
static lv_obj_t *s_lbl_artist   = NULL;
static lv_obj_t *s_lbl_album    = NULL;

/* Progress */
static lv_obj_t *s_prog_bar     = NULL;

static lv_obj_t *s_lbl_elapsed  = NULL;
static lv_obj_t *s_lbl_total    = NULL;

/* Controls */
static lv_obj_t *s_btn_prev     = NULL;
static lv_obj_t *s_btn_play     = NULL;
static lv_obj_t *s_btn_next     = NULL;
static lv_obj_t *s_lbl_play_ic  = NULL;  /* MDI icon label */

/* Volume knob */
static lv_obj_t *s_vol_arc      = NULL;
static lv_obj_t *s_lbl_vol      = NULL;

/* FFT visualiser */
static lv_obj_t *s_fft_cont     = NULL;

/* Vinyl animation timer */
static esp_timer_handle_t s_vinyl_timer = NULL;
static float              s_vinyl_angle = 0.0f;
static bool               s_vinyl_spinning = false;

/* Clock update timer */
static esp_timer_handle_t s_clock_timer = NULL;

/* â”€â”€ Helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/

static void format_time_ms(char *buf, size_t len, uint32_t ms)
{
    uint32_t s  = ms / 1000;
    uint32_t m  = s / 60;
    s %= 60;
    snprintf(buf, len, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

/* â”€â”€ Button callbacks â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
static void btn_prev_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Previous track");
    event_post_u32(EVT_PLAYBACK_STATE, 0xA1);  /* custom: prev command */
}

static void btn_play_cb(lv_event_t *e)
{
    const app_state_t *st = app_state_get();
    playback_state_t next = (st->playback == PLAYBACK_PLAYING)
                            ? PLAYBACK_PAUSED : PLAYBACK_PLAYING;
    app_state_set_playback(next);
}

static void btn_next_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Next track");
    event_post_u32(EVT_PLAYBACK_STATE, 0xA2);  /* custom: next command */
}

static void vol_arc_cb(lv_event_t *e)
{
    lv_obj_t *arc = lv_event_get_target(e);
    int16_t val = lv_arc_get_value(arc);
    app_state_set_volume((uint8_t)val);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", val);
    lv_label_set_text(s_lbl_vol, buf);
}

/* â”€â”€ Vinyl rotation timer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
static void vinyl_timer_cb(void *arg)
{
    if (!s_vinyl_spinning) return;
    /* 0.5rpm = 3deg/s â†’ at 30Hz callback = 0.1deg/tick */
    s_vinyl_angle += 0.1f;
    if (s_vinyl_angle >= 360.0f) s_vinyl_angle -= 360.0f;

    /* lv_img rotate requires the lock â€” but we're in a timer callback.
     * We post a flag and let the LVGL task apply. For simplicity in Fase 1
     * we use lv_obj_set_style_transform_angle which is safe in timer. */
    lv_obj_set_style_transform_angle(s_art_cont,
        (int16_t)(s_vinyl_angle * 10), 0);   /* LVGL uses 0.1deg units */
}

/* â”€â”€ Clock timer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
static void clock_timer_cb(void *arg)
{
    /* Update time label */
    /* NOTE: In real code, use esp_sntp or RTC. Placeholder uses tick count. */
    uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t h = (sec / 3600) % 24;
    uint32_t m = (sec / 60) % 60;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)h, (unsigned long)m);
    lv_label_set_text(s_lbl_time, buf);
}

/* â”€â”€ Screen construction â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/

lv_obj_t *screen_now_playing_create(void)
{
    ESP_LOGI(TAG, "Creating Now Playing screen");

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);

    /* â”€â”€ Status bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    lv_obj_t *status_bar = lv_obj_create(s_screen);
    lv_obj_set_size(status_bar, SCR_W, STATUS_H);
    lv_obj_align(status_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_make(0x0A, 0x0A, 0x0E), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_hor(status_bar, 12, 0);
    lv_obj_set_style_pad_ver(status_bar, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar, LV_SCROLLBAR_MODE_OFF);

    /* WiFi icon */
    s_lbl_wifi = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_LEFT_MID, 0, 0);

    /* Clock */
    s_lbl_time = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_time, "--:--");
    lv_obj_set_style_text_color(s_lbl_time, COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, 0);

    /* Quality badge */
    s_lbl_quality = lv_label_create(status_bar);
    lv_label_set_text(s_lbl_quality, "SPOTIFY HQ");
    lv_obj_set_style_text_color(s_lbl_quality, COL_ACCENT, 0);
    lv_obj_set_style_text_font(s_lbl_quality, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_quality, LV_ALIGN_RIGHT_MID, 0, 0);

    /* â”€â”€ Album art container â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    s_art_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_art_cont, ART_W, ART_H);
    lv_obj_set_pos(s_art_cont, ART_X, ART_Y);
    lv_obj_set_style_bg_color(s_art_cont, COL_SURFACE, 0);
    lv_obj_set_style_bg_opa(s_art_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_art_cont, 0, 0);
    lv_obj_set_style_radius(s_art_cont, 24, 0);
    lv_obj_set_style_clip_corner(s_art_cont, true, 0);
    lv_obj_set_style_pad_all(s_art_cont, 0, 0);
    lv_obj_set_style_shadow_width(s_art_cont, 40, 0);
    lv_obj_set_style_shadow_color(s_art_cont, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_shadow_opa(s_art_cont, LV_OPA_60, 0);
    lv_obj_set_style_transform_pivot_x(s_art_cont, ART_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_art_cont, ART_H / 2, 0);
    lv_obj_set_scrollbar_mode(s_art_cont, LV_SCROLLBAR_MODE_OFF);

    /* Album art image â€” placeholder colour until art arrives */
    s_art_img = lv_obj_create(s_art_cont);
    lv_obj_set_size(s_art_img, ART_W, ART_H);
    lv_obj_set_style_bg_color(s_art_img, lv_color_make(0x25, 0x25, 0x2E), 0);
    lv_obj_set_style_border_width(s_art_img, 0, 0);
    lv_obj_set_style_pad_all(s_art_img, 0, 0);

    /* Frosted glass overlay â€” semi-transparent tint from palette */
    s_art_overlay = lv_obj_create(s_art_cont);
    lv_obj_set_size(s_art_overlay, ART_W, ART_H);
    lv_obj_set_style_bg_color(s_art_overlay, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(s_art_overlay, LV_OPA_10, 0);
    lv_obj_set_style_border_width(s_art_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_art_overlay, 0, 0);

    /* â”€â”€ Track info panel â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    lv_obj_t *info_panel = lv_obj_create(s_screen);
    lv_obj_set_size(info_panel, INFO_W, ART_H);
    lv_obj_set_pos(info_panel, INFO_X, ART_Y);
    lv_obj_set_style_bg_opa(info_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_panel, 0, 0);
    lv_obj_set_style_pad_all(info_panel, 0, 0);
    lv_obj_set_scrollbar_mode(info_panel, LV_SCROLLBAR_MODE_OFF);

    /* Title */
    s_lbl_title = lv_label_create(info_panel);
    lv_label_set_text(s_lbl_title, "Connecting to Spotify...");
    lv_obj_set_width(s_lbl_title, INFO_W);
    lv_label_set_long_mode(s_lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s_lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_LEFT, 0, 8);

    /* Artist */
    s_lbl_artist = lv_label_create(info_panel);
    lv_label_set_text(s_lbl_artist, "â€”");
    lv_obj_set_width(s_lbl_artist, INFO_W);
    lv_label_set_long_mode(s_lbl_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_lbl_artist, COL_TEXT2, 0);
    lv_obj_set_style_text_font(s_lbl_artist, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_lbl_artist, s_lbl_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    /* Album */
    s_lbl_album = lv_label_create(info_panel);
    lv_label_set_text(s_lbl_album, "");
    lv_obj_set_width(s_lbl_album, INFO_W);
    lv_label_set_long_mode(s_lbl_album, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_lbl_album, COL_TEXT2, 0);
    lv_obj_set_style_text_font(s_lbl_album, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_lbl_album, s_lbl_artist, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    /* â”€â”€ Progress bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    lv_obj_t *prog_cont = lv_obj_create(info_panel);
    lv_obj_set_size(prog_cont, INFO_W, 24);
    lv_obj_align_to(prog_cont, s_lbl_album, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_style_bg_opa(prog_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prog_cont, 0, 0);
    lv_obj_set_style_pad_all(prog_cont, 0, 0);
    lv_obj_set_scrollbar_mode(prog_cont, LV_SCROLLBAR_MODE_OFF);

    s_prog_bar = lv_bar_create(prog_cont);
    lv_obj_set_size(s_prog_bar, INFO_W - 10, 6);
    lv_bar_set_range(s_prog_bar, 0, 1000);
    lv_bar_set_value(s_prog_bar, 0, LV_ANIM_OFF);
    lv_obj_align(s_prog_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_prog_bar, lv_color_make(0x40, 0x40, 0x50), 0);
    lv_obj_set_style_bg_color(s_prog_bar, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_prog_bar, 3, 0);
    lv_obj_set_style_radius(s_prog_bar, 3, LV_PART_INDICATOR);

    /* Time labels */
    s_lbl_elapsed = lv_label_create(info_panel);
    lv_label_set_text(s_lbl_elapsed, "0:00");
    lv_obj_set_style_text_color(s_lbl_elapsed, COL_TEXT2, 0);
    lv_obj_set_style_text_font(s_lbl_elapsed, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_lbl_elapsed, prog_cont, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    s_lbl_total = lv_label_create(info_panel);
    lv_label_set_text(s_lbl_total, "0:00");
    lv_obj_set_style_text_color(s_lbl_total, COL_TEXT2, 0);
    lv_obj_set_style_text_font(s_lbl_total, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_lbl_total, prog_cont, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);

    /* â”€â”€ Playback controls â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    lv_obj_t *ctrl_cont = lv_obj_create(info_panel);
    lv_obj_set_size(ctrl_cont, INFO_W, 80);
    lv_obj_align_to(ctrl_cont, s_lbl_elapsed, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_set_style_bg_opa(ctrl_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_cont, 0, 0);
    lv_obj_set_style_pad_all(ctrl_cont, 0, 0);
    lv_obj_set_scrollbar_mode(ctrl_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(ctrl_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctrl_cont, 24, 0);

    /* Helper: create icon button */
#define MAKE_CTRL_BTN(var, icon_str) \
    var = lv_btn_create(ctrl_cont); \
    lv_obj_set_size(var, 64, 64); \
    lv_obj_set_style_bg_color(var, COL_CTRL, 0); \
    lv_obj_set_style_radius(var, 32, 0); \
    lv_obj_set_style_border_width(var, 0, 0); \
    lv_obj_set_style_shadow_width(var, 0, 0); \
    { lv_obj_t *ic = lv_label_create(var); \
      lv_label_set_text(ic, icon_str); \
      lv_obj_set_style_text_font(ic, &lv_font_montserrat_14, 0); \
      lv_obj_set_style_text_color(ic, COL_TEXT, 0); \
      lv_obj_center(ic); }

    MAKE_CTRL_BTN(s_btn_prev, LV_SYMBOL_PREV)
    lv_obj_add_event_cb(s_btn_prev, btn_prev_cb, LV_EVENT_CLICKED, NULL);

    MAKE_CTRL_BTN(s_btn_play, LV_SYMBOL_PLAY)
    lv_obj_set_size(s_btn_play, 72, 72);
    lv_obj_set_style_bg_color(s_btn_play, COL_ACCENT, 0);
    lv_obj_set_style_radius(s_btn_play, 36, 0);
    lv_obj_add_event_cb(s_btn_play, btn_play_cb, LV_EVENT_CLICKED, NULL);
    /* Keep reference to the icon label for playâ†”pause morphing */
    s_lbl_play_ic = lv_obj_get_child(s_btn_play, 0);

    MAKE_CTRL_BTN(s_btn_next, LV_SYMBOL_NEXT)
    lv_obj_add_event_cb(s_btn_next, btn_next_cb, LV_EVENT_CLICKED, NULL);

    /* â”€â”€ Volume arc knob â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    lv_obj_t *vol_cont = lv_obj_create(info_panel);
    lv_obj_set_size(vol_cont, 100, 100);
    lv_obj_align_to(vol_cont, ctrl_cont, LV_ALIGN_OUT_RIGHT_MID, 16, 0);
    lv_obj_set_style_bg_opa(vol_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_cont, 0, 0);
    lv_obj_set_style_pad_all(vol_cont, 0, 0);
    lv_obj_set_scrollbar_mode(vol_cont, LV_SCROLLBAR_MODE_OFF);

    s_vol_arc = lv_arc_create(vol_cont);
    lv_obj_set_size(s_vol_arc, 90, 90);
    lv_obj_center(s_vol_arc);
    lv_arc_set_rotation(s_vol_arc, 135);
    lv_arc_set_bg_angles(s_vol_arc, 0, 270);
    lv_arc_set_range(s_vol_arc, 0, 100);
    const app_state_t *st = app_state_get();
    lv_arc_set_value(s_vol_arc, st->volume_pct);
    lv_obj_set_style_arc_color(s_vol_arc, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_vol_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_vol_arc, lv_color_make(0x30, 0x30, 0x40), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_vol_arc, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_vol_arc, vol_arc_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_lbl_vol = lv_label_create(vol_cont);
    char vbuf[8];
    snprintf(vbuf, sizeof(vbuf), "%d", st->volume_pct);
    lv_label_set_text(s_lbl_vol, vbuf);
    lv_obj_set_style_text_color(s_lbl_vol, COL_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_vol, &lv_font_montserrat_14, 0);
    lv_obj_center(s_lbl_vol);

    /* â”€â”€ Audio mode widget â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    widget_audio_mode_create(info_panel);

    /* â”€â”€ FFT visualiser â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    s_fft_cont = lv_obj_create(s_screen);
    lv_obj_set_size(s_fft_cont, SCR_W, FFT_H);
    lv_obj_align(s_fft_cont, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_fft_cont, lv_color_make(0x08, 0x08, 0x0C), 0);
    lv_obj_set_style_bg_opa(s_fft_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_fft_cont, 0, 0);
    lv_obj_set_style_pad_all(s_fft_cont, 0, 0);
    lv_obj_set_scrollbar_mode(s_fft_cont, LV_SCROLLBAR_MODE_OFF);

    widget_fft_create(s_fft_cont, SCR_W, FFT_H);

    /* â”€â”€ Timers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    /* Vinyl rotation: 30Hz */
    esp_timer_create_args_t vtimer = {
        .callback = vinyl_timer_cb, .name = "vinyl"
    };
    esp_timer_create(&vtimer, &s_vinyl_timer);
    /* Start only when playing */

    /* Clock update: 10s */
    esp_timer_create_args_t ctimer = {
        .callback = clock_timer_cb, .name = "clock"
    };
    esp_timer_create(&ctimer, &s_clock_timer);
    esp_timer_start_periodic(s_clock_timer, 10ULL * 1000000);
    clock_timer_cb(NULL);   /* immediate update */

    ESP_LOGI(TAG, "Now Playing screen created");
    return s_screen;
}

/* â”€â”€ Callbacks from ui_main â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/

void screen_now_playing_on_track_changed(void)
{
    const app_state_t *st = app_state_get();

    lv_label_set_text(s_lbl_title,  st->track.title);
    lv_label_set_text(s_lbl_artist, st->track.artist);
    lv_label_set_text(s_lbl_album,  st->track.album);

    char buf[16];
    format_time_ms(buf, sizeof(buf), st->track.duration_ms);
    lv_label_set_text(s_lbl_total, buf);

    lv_bar_set_value(s_prog_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_lbl_elapsed, "0:00");

    /* Reset art to placeholder while new art downloads */
    lv_obj_set_style_bg_color(s_art_img, lv_color_make(0x25, 0x25, 0x2E), 0);
}

void screen_now_playing_on_playback_state(playback_state_t state)
{
    if (state == PLAYBACK_PLAYING) {
        lv_label_set_text(s_lbl_play_ic, LV_SYMBOL_PAUSE);
        /* Start vinyl rotation */
        if (!s_vinyl_spinning) {
            s_vinyl_spinning = true;
            esp_timer_start_periodic(s_vinyl_timer, 33333); /* ~30fps */
        }
    } else {
        lv_label_set_text(s_lbl_play_ic, LV_SYMBOL_PLAY);
        s_vinyl_spinning = false;
        esp_timer_stop(s_vinyl_timer);
    }
}

void screen_now_playing_on_progress(uint32_t position_ms)
{
    const app_state_t *st = app_state_get();
    uint32_t dur = st->track.duration_ms;
    if (dur == 0) return;

    int32_t pct = (int32_t)((position_ms * 1000) / dur);
    /* Smooth animation to new value */
    lv_bar_set_value(s_prog_bar, pct, LV_ANIM_ON);

    char buf[16];
    format_time_ms(buf, sizeof(buf), position_ms);
    lv_label_set_text(s_lbl_elapsed, buf);
}

void screen_now_playing_on_album_art_ready(void)
{
    const app_state_t *st = app_state_get();
    if (!st->album_art_ready || !st->album_art_buf) return;

    /* Configure LVGL image descriptor pointing to PSRAM buffer */
    s_art_dsc.header.w      = (uint32_t)st->album_art_w;
    s_art_dsc.header.h      = (uint32_t)st->album_art_h;
    s_art_dsc.header.cf     = LV_IMG_CF_RGB888;
    s_art_dsc.data_size     = st->album_art_w * st->album_art_h * 3;
    s_art_dsc.data          = (const uint8_t *)st->album_art_buf;

    /* Replace placeholder with real art */
    lv_obj_del(s_art_img);
    s_art_img = lv_img_create(s_art_cont);
    lv_img_set_src(s_art_img, &s_art_dsc);
    lv_obj_set_size(s_art_img, ART_W, ART_H);
    lv_obj_align(s_art_img, LV_ALIGN_CENTER, 0, 0);

    ESP_LOGI(TAG, "Album art displayed %ux%u", (unsigned)st->album_art_w, (unsigned)st->album_art_h);
}

void screen_now_playing_on_palette_changed(void)
{
    const app_state_t *st = app_state_get();
    if (!st->palette_ready) return;

    uint32_t p  = st->palette.primary;
    uint32_t ac = st->palette.accent;
    lv_color_t col_p  = lv_color_make((p>>16)&0xFF,(p>>8)&0xFF, p&0xFF);
    lv_color_t col_ac = lv_color_make((ac>>16)&0xFF,(ac>>8)&0xFF,ac&0xFF);

    /* Update accent colour on progress bar + volume arc + play button */
    lv_obj_set_style_bg_color(s_prog_bar, col_p, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_vol_arc, col_ac, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_btn_play, col_ac, 0);

    /* Subtle tint on art overlay from dominant colour */
    lv_obj_set_style_bg_color(s_art_overlay, col_p, 0);
    lv_obj_set_style_bg_opa(s_art_overlay, LV_OPA_20, 0);

    ESP_LOGI(TAG, "Palette applied to Now Playing");
}
