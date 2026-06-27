/**
 * @file screen_music.c
 * @brief Pantalla de m+�sica completa para ESP32-P4
 * - Artwork via HTTP + JPEG decode
 * - Barra de progreso t+�ctil
 * - Lista de reproducci+�n
 * - Controles play/pause/next/prev
 */

#include "screen_music.h"
#include "ha_client.h"
#include "driver/jpeg_decode.h"
#include "ma_client.h"
#include "spotify_client.h"
#include "volumio_client.h"
#include "screen_audio.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "screen_music";

/* ������ UI Objects ������ */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_artwork      = NULL;
static lv_obj_t *s_title        = NULL;
static lv_obj_t *s_artist       = NULL;
static lv_obj_t *s_album        = NULL;
static lv_obj_t *s_btn_prev     = NULL;
static lv_obj_t *s_btn_play     = NULL;
static lv_obj_t *s_btn_next     = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_time_elapsed = NULL;
static lv_obj_t *s_time_total   = NULL;
static lv_obj_t *s_playlist     = NULL;
static lv_obj_t *s_back_btn     = NULL;

/* Estado actual */
static int s_duration    = 0;
static int s_elapsed     = 0;
static bool s_playing    = false;
static int  s_volume     = 50;
static bool s_muted      = false;
static int  s_vol_before_mute = 50;
static lv_obj_t *s_vol_label   = NULL;
static lv_obj_t *s_mute_btn    = NULL;
static char s_player_id[64] = {0};
static char s_image_url[256] = {0};
static lv_img_dsc_t s_artwork_dsc = {0};
static uint8_t *s_artwork_buf = NULL;

/* Callback de navegaci+�n */
static lv_event_cb_t s_back_cb = NULL;

/* ������ Helpers ������ */
static void sanitize(const char *src, char *dst, size_t max)
{
    size_t i=0,j=0;
    unsigned char *s=(unsigned char*)src;
    while(*s&&j<max-1){
        if(*s<0x80){dst[j++]=*s++;}
        else if((*s&0xE0)==0xC0){
            unsigned int cp=(*s&0x1F)<<6;s++;cp|=(*s&0x3F);s++;
            switch(cp){
                case 0xE1:dst[j++]='a';break;case 0xE9:dst[j++]='e';break;
                case 0xED:dst[j++]='i';break;case 0xF3:dst[j++]='o';break;
                case 0xFA:dst[j++]='u';break;case 0xF1:dst[j++]='n';break;
                case 0xE3:dst[j++]='a';break;case 0xF5:dst[j++]='o';break;
                case 0xE7:dst[j++]='c';break;case 0xE2:dst[j++]='a';break;
                case 0xEA:dst[j++]='e';break;case 0xF4:dst[j++]='o';break;
                case 0xC1:dst[j++]='A';break;case 0xC9:dst[j++]='E';break;
                case 0xD3:dst[j++]='O';break;case 0xDA:dst[j++]='U';break;
                case 0xD1:dst[j++]='N';break;case 0xC7:dst[j++]='C';break;
                default:if(j>0&&dst[j-1]!=' ')dst[j++]=' ';break;
            }
        } else {while(*s&&(*s&0xC0)==0x80)s++;if(*s>=0x80)s++;}
    }
    dst[j]=0;
}

static void format_time(int secs, char *buf, size_t len)
{
    snprintf(buf, len, "%d:%02d", secs/60, secs%60);
}

/* ������ Artwork download ������ */
static void artwork_task(void *arg)
{
    char url[256];
    strncpy(url, (char*)arg, sizeof(url)-1);
    free(arg);

    ESP_LOGI(TAG, "Descargando artwork: %s", url);

    esp_http_client_config_t cfg = {
        .url            = url,
        .timeout_ms     = 8000,
        .buffer_size    = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { vTaskDelete(NULL); return; }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        vTaskDelete(NULL); return;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len <= 0 || content_len > 512*1024) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL); return;
    }

    uint8_t *buf = heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL); return;
    }

    int read = 0, total = 0;
    while (total < content_len) {
        read = esp_http_client_read(client, (char*)buf+total, content_len-total);
        if (read <= 0) break;
        total += read;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total < content_len) { free(buf); vTaskDelete(NULL); return; }

    ESP_LOGI(TAG, "Artwork descargado: %d bytes", total);

    /* Decodificar JPEG a RGB565 usando esp_jpeg */
    /* Decode JPEG usando esp_driver_jpeg */
    jpeg_decode_engine_cfg_t engine_cfg = { .timeout_ms = 10000 };
    jpeg_decoder_handle_t jpeg_handle = NULL;
    esp_err_t jer = jpeg_new_decoder_engine(&engine_cfg, &jpeg_handle);

    uint16_t *rgb_buf = NULL;
    uint32_t out_w = 240, out_h = 240;

    if (jer == ESP_OK) {
        jpeg_decode_cfg_t decode_cfg = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        };
        /* Use jpeg_alloc_decoder_mem for aligned allocation */
        /* Use aligned allocation for JPEG decoder */
        jpeg_decode_memory_alloc_cfg_t mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
        };
        size_t out_buf_size = 640 * 640 * 2;
        uint8_t *aligned_buf = NULL;
        aligned_buf = (uint8_t*)jpeg_alloc_decoder_mem(out_buf_size, &mem_cfg, &out_buf_size);
        if (!aligned_buf) {
            /* Fallback to PSRAM */
            out_buf_size = 640 * 640 * 2;
            aligned_buf = heap_caps_aligned_alloc(64, out_buf_size, MALLOC_CAP_SPIRAM);
        }
        if (aligned_buf) {
            uint32_t decoded_size = 0;
            jer = jpeg_decoder_process(jpeg_handle, &decode_cfg,
                      buf, total, aligned_buf, (uint32_t)out_buf_size, &decoded_size);
            if (jer != ESP_OK) {
                ESP_LOGW(TAG, "JPEG decode err: %d", jer);
                free(aligned_buf); aligned_buf = NULL;
            } else {
                rgb_buf = (uint16_t*)aligned_buf;
                ESP_LOGI(TAG, "JPEG OK, %ld bytes out", decoded_size);
            }
        }
        jpeg_del_decoder_engine(jpeg_handle);
    }
    free(buf);

    if (!rgb_buf) { vTaskDelete(NULL); return; }

    if (s_artwork_buf) { free(s_artwork_buf); s_artwork_buf = NULL; }
    s_artwork_buf = (uint8_t*)rgb_buf;

    /* Image is 640x640, display area is 240x240 - scale = 240/640 * 256 = 96 */
    bsp_display_lock(0);
    if (s_artwork) {
        s_artwork_dsc.header.cf       = LV_IMG_CF_TRUE_COLOR;
        s_artwork_dsc.header.w        = 640;
        s_artwork_dsc.header.h        = 640;
        s_artwork_dsc.data_size       = 640 * 640 * 2;
        s_artwork_dsc.data            = (uint8_t*)rgb_buf;
        lv_obj_clean(s_artwork);
        lv_obj_t *img = lv_img_create(s_artwork);
        lv_img_set_src(img, &s_artwork_dsc);
        lv_img_set_zoom(img, 96); /* 240/640 * 256 = 96 */
        lv_obj_center(img);
        lv_obj_invalidate(s_artwork);
    }
    bsp_display_unlock();

    vTaskDelete(NULL);
}

static void download_artwork(const char *url)
{
    if (!url || strlen(url) == 0) return;
    if (strcmp(url, s_image_url) == 0) return; /* misma imagen */
    strncpy(s_image_url, url, sizeof(s_image_url)-1);
    char *url_copy = strdup(url);
    if (url_copy)
        xTaskCreate(artwork_task, "artwork", 8192, url_copy, 3, NULL);
}

/* ������ Progress bar timer ������ */
static void progress_timer_cb(lv_timer_t *t)
{
    if (!s_playing || s_duration <= 0) return;
    s_elapsed++;
    if (s_elapsed > s_duration) s_elapsed = s_duration;

    bsp_display_lock(0);
    lv_bar_set_value(s_progress_bar, (s_elapsed * 100) / s_duration, LV_ANIM_OFF);
    char buf[12];
    format_time(s_elapsed, buf, sizeof(buf));
    lv_label_set_text(s_time_elapsed, buf);
    bsp_display_unlock();
}

/* ������ Progress bar touch ������ */
static void progress_cb(lv_event_t *e)
{
    lv_obj_t *bar = lv_event_get_target(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    int w = lv_obj_get_width(bar);
    int x = p.x - lv_obj_get_x(bar);
    if (x < 0) x = 0;
    if (x > w) x = w;
    int pct = (x * 100) / w;
    int seek_pos = (pct * s_duration) / 100;
    spotify_seek(seek_pos * 1000);
}

/* ������ Button callbacks ������ */
static lv_timer_t *s_vol_hide_timer = NULL;
static uint32_t s_vol_last_ms = 0;
#define VOL_DEBOUNCE_MS 400
#define VOL_STEP 5

static void vol_label_hide_cb(lv_timer_t *t) {
    bsp_display_lock(0);
    if (s_vol_label) lv_label_set_text(s_vol_label, "");
    bsp_display_unlock();
    lv_timer_del(s_vol_hide_timer);
    s_vol_hide_timer = NULL;
}

static void show_vol_label(void) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", s_volume);
    bsp_display_lock(0);
    if (s_vol_label) lv_label_set_text(s_vol_label, buf);
    bsp_display_unlock();
    if (s_vol_hide_timer) lv_timer_del(s_vol_hide_timer);
    s_vol_hide_timer = lv_timer_create(vol_label_hide_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_vol_hide_timer, 1);
}

static void vol_down_cb(lv_event_t *e) {
    uint32_t now = lv_tick_get();
    if (now - s_vol_last_ms < VOL_DEBOUNCE_MS) return;
    s_vol_last_ms = now;
    s_volume -= VOL_STEP;
    if (s_volume < 0) s_volume = 0;
    ha_media_set_volume("media_player.spotify_premiun", s_volume / 100.0f);
    volumio_set_volume(-VOL_STEP);
    show_vol_label();
}

static void vol_up_cb(lv_event_t *e) {
    uint32_t now = lv_tick_get();
    if (now - s_vol_last_ms < VOL_DEBOUNCE_MS) return;
    s_vol_last_ms = now;
    s_volume += VOL_STEP;
    if (s_volume > 100) s_volume = 100;
    ha_media_set_volume("media_player.spotify_premiun", s_volume / 100.0f);
    volumio_set_volume(VOL_STEP);
    show_vol_label();
}

static void mute_cb(lv_event_t *e)
{
    s_muted = !s_muted;
    if (s_muted) {
        s_vol_before_mute = s_volume;
        ha_media_set_volume("media_player.spotify_premiun", 0.0f);
    } else {
        ha_media_set_volume("media_player.spotify_premiun", s_vol_before_mute / 100.0f);
        s_volume = s_vol_before_mute;
    }
    bsp_display_lock(0);
    if (s_mute_btn) {
        lv_obj_set_style_bg_color(s_mute_btn,
            s_muted ? lv_color_make(0xAA,0x22,0x22) : lv_color_make(0x2A,0x2A,0x3A), 0);
        lv_obj_t *lbl = lv_obj_get_child(s_mute_btn, 0);
        if (lbl) lv_label_set_text(lbl, s_muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MID);
    }
    if (s_vol_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), s_muted ? "Mute" : "%d%%", s_muted ? 0 : s_volume);
        lv_label_set_text(s_vol_label, buf);
    }
    bsp_display_unlock();
}

static void play_cb(lv_event_t *e) { ESP_LOGI(TAG,"PLAY"); ha_service_call("media_player","media_play_pause","{\"entity_id\":\"media_player.spotify_premiun\"}"); }
static void next_cb(lv_event_t *e) { ESP_LOGI(TAG,"NEXT"); ha_service_call("media_player","media_next_track","{\"entity_id\":\"media_player.spotify_premiun\"}"); }
static void prev_cb(lv_event_t *e) { ESP_LOGI(TAG,"PREV"); ha_service_call("media_player","media_previous_track","{\"entity_id\":\"media_player.spotify_premiun\"}"); }

/* ������ Public: actualizar desde MA callback ������ */
void screen_music_update(const ma_player_t *p)
{
    if (!s_screen) return;
    strncpy(s_player_id, p->player_id, sizeof(s_player_id)-1);
    s_playing  = p->playing;
    /* no volume in ma_player_t directly */
    s_duration = p->current_item.duration;
    s_elapsed  = p->elapsed_time;

    bsp_display_lock(0);

    /* T+�tulo */
    char title[128]={0}, artist[128]={0};
    sanitize(p->current_item.title,  title,  sizeof(title));
    sanitize(p->current_item.artist, artist, sizeof(artist));
    if (strlen(title)==0) strncpy(title,"Sin reproduccion",sizeof(title)-1);
    lv_label_set_text(s_title,  title);
    lv_label_set_text(s_artist, artist);

    /* Play/Pause */
    lv_label_set_text(lv_obj_get_child(s_btn_play, 0),
        p->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_bg_color(s_btn_play, p->playing ? lv_color_make(0x1D,0xB9,0x54) : lv_color_make(0x7C,0x5C,0xFC), 0);

    /* Progreso */
    if (s_duration > 0) {
        lv_bar_set_value(s_progress_bar, (s_elapsed*100)/s_duration, LV_ANIM_OFF);
        char et[12], tt[12];
        format_time(s_elapsed, et, sizeof(et));
        format_time(s_duration, tt, sizeof(tt));
        lv_label_set_text(s_time_elapsed, et);
        lv_label_set_text(s_time_total,   tt);
    }

    bsp_display_unlock();

    /* Artwork */
    if (strlen(p->current_item.image_url) > 0) { /* artwork download disabled - SDIO conflict */ }
}


/* Playlists grid */
#define MAX_PLAYLISTS 20
static char s_pl_uris[MAX_PLAYLISTS][128] = {0};
static int  s_pl_count = 0;

static void playlist_item_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_pl_count) {
        ESP_LOGI(TAG, "Playlist: %s", s_pl_uris[idx]);
        volumio_play_playlist(s_pl_uris[idx]);
    }
}

void screen_music_add_playlist_item(const char *name, const char *uri) {
    if (!s_playlist || s_pl_count >= MAX_PLAYLISTS) return;
    strncpy(s_pl_uris[s_pl_count], uri, sizeof(s_pl_uris[0])-1);
    bsp_display_lock(0);
    lv_obj_t *btn = lv_list_add_btn(s_playlist, LV_SYMBOL_AUDIO, name);
    lv_obj_set_height(btn, 60);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x1E,0x1E,0x2A), 0);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x7C,0x5C,0xFC), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_make(0x33,0x33,0x44), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *icon = lv_obj_get_child(btn, 0);
    if (icon) {
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(icon, lv_color_make(0x7C,0x5C,0xFC), 0);
    }
    lv_obj_t *lbl = lv_obj_get_child(btn, 1);
    if (lbl) {
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, 440);
    }
    lv_obj_add_event_cb(btn, playlist_item_cb, LV_EVENT_CLICKED, (void*)(intptr_t)s_pl_count);
    bsp_display_unlock();
    s_pl_count++;
}

void screen_music_add_queue_item(const char *title, const char *artist, int index)
{
    if (!s_playlist) return;
    bsp_display_lock(0);
    char buf[128]={0};
    char ts[64]={0}, as[64]={0};
    sanitize(title, ts, sizeof(ts));
    sanitize(artist, as, sizeof(as));
    snprintf(buf, sizeof(buf), "%d. %s - %s", index+1, ts, as);
    lv_list_add_btn(s_playlist, LV_SYMBOL_AUDIO, buf);
    bsp_display_unlock();
}

/* ������ Build screen ������ */
static void go_audio_cb(lv_event_t *e) {
    lv_obj_t *audio_scr = screen_audio_get_screen();
    if (audio_scr)
        lv_scr_load_anim(audio_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

lv_obj_t *screen_music_create(lv_event_cb_t back_cb)
{
    s_back_cb = back_cb;

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_make(0x12,0x12,0x12), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    /* ������ Back button ������ */
    s_back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_back_btn, 110, 44);
    lv_obj_align(s_back_btn, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_make(0x2A,0x2A,0x3A), 0);
    lv_obj_set_style_radius(s_back_btn, 10, 0);
    lv_obj_set_style_border_color(s_back_btn, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_set_style_border_width(s_back_btn, 1, 0);
    lv_obj_t *blbl = lv_label_create(s_back_btn);
    lv_label_set_text(blbl, LV_SYMBOL_LEFT "  Inicio");
    lv_obj_set_style_text_color(blbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(blbl, &lv_font_montserrat_14, 0);
    lv_obj_center(blbl);
    lv_obj_add_event_cb(s_back_btn, back_cb, LV_EVENT_CLICKED, NULL);

    /* Boton Sonido */
    lv_obj_t *audio_btn = lv_btn_create(s_screen);
    lv_obj_set_size(audio_btn, 110, 44);
    lv_obj_align(audio_btn, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_bg_color(audio_btn, lv_color_make(0x2A,0x2A,0x3A), 0);
    lv_obj_set_style_bg_color(audio_btn, lv_color_make(0x7C,0x5C,0xFC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(audio_btn, 10, 0);
    lv_obj_set_style_border_color(audio_btn, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_set_style_border_width(audio_btn, 1, 0);
    lv_obj_set_style_shadow_width(audio_btn, 0, 0);
    lv_obj_set_style_outline_width(audio_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(audio_btn, 0, LV_STATE_FOCUSED);
    lv_obj_t *albl = lv_label_create(audio_btn);
    lv_label_set_text(albl, "Sonido  " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(albl, lv_color_white(), 0);
    lv_obj_set_style_text_font(albl, &lv_font_montserrat_14, 0);
    lv_obj_center(albl);
    lv_obj_add_event_cb(audio_btn, go_audio_cb, LV_EVENT_CLICKED, NULL);

    /* ������ Header ������ */
    lv_obj_t *hdr = lv_label_create(s_screen);
    lv_label_set_text(hdr, LV_SYMBOL_AUDIO "  Spotify");
    lv_obj_set_style_text_color(hdr, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 14);

    /* �=+�=+ LEFT PANEL ��� Artwork + Controls �=+�=+ */
    lv_obj_t *left = lv_obj_create(s_screen);
    lv_obj_set_size(left, 460, 500);
    lv_obj_align(left, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_color(left, lv_color_make(0x1A,0x1A,0x1A), 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_radius(left, 16, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(left, LV_SCROLLBAR_MODE_OFF);

    /* Artwork - 240x240 */
    s_artwork = lv_obj_create(left);
    lv_obj_set_size(s_artwork, 240, 240);
    lv_obj_align(s_artwork, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(s_artwork, lv_color_make(0x2A,0x2A,0x2A), 0);
    lv_obj_set_style_border_width(s_artwork, 0, 0);
    lv_obj_set_style_radius(s_artwork, 16, 0);
    lv_obj_t *art_icon = lv_label_create(s_artwork);
    lv_label_set_text(art_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(art_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(art_icon, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_center(art_icon);

    /* Title y=262 */
    s_title = lv_label_create(left);
    lv_label_set_text(s_title, "Sin reproduccion");
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_title, 420);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 260);

    /* Artist y=288 */
    s_artist = lv_label_create(left);
    lv_label_set_text(s_artist, "");
    lv_obj_set_style_text_color(s_artist, lv_color_make(0xAA,0xAA,0xAA), 0);
    lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_artist, 420);
    lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_artist, LV_ALIGN_TOP_MID, 0, 286);

    /* Progress bar y=316 */
    s_progress_bar = lv_bar_create(left);
    lv_obj_set_size(s_progress_bar, 400, 6);
    lv_obj_align(s_progress_bar, LV_ALIGN_TOP_MID, 0, 316);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(0x44,0x44,0x44), 0);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(0x7C,0x5C,0xFC), LV_PART_INDICATOR);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_progress_bar, progress_cb, LV_EVENT_CLICKED, NULL);

    /* Time labels y=326 - peque+�os */
    s_time_elapsed = lv_label_create(left);
    lv_label_set_text(s_time_elapsed, "0:00");
    lv_obj_set_style_text_color(s_time_elapsed, lv_color_make(0x66,0x66,0x66), 0);
    lv_obj_set_style_text_font(s_time_elapsed, &lv_font_montserrat_14, 0);
    lv_obj_align(s_time_elapsed, LV_ALIGN_TOP_LEFT, 28, 322);

    s_time_total = lv_label_create(left);
    lv_label_set_text(s_time_total, "0:00");
    lv_obj_set_style_text_color(s_time_total, lv_color_make(0x66,0x66,0x66), 0);
    lv_obj_set_style_text_font(s_time_total, &lv_font_montserrat_14, 0);
    lv_obj_align(s_time_total, LV_ALIGN_TOP_RIGHT, -28, 322);

    /* Controls - posicion absoluta, sin flex */
    lv_obj_t *ctrl = lv_obj_create(left);
    lv_obj_set_size(ctrl, 420, 80);
    lv_obj_align(ctrl, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_obj_set_style_bg_opa(ctrl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl, 0, 0);
    lv_obj_set_style_pad_all(ctrl, 0, 0);
    lv_obj_clear_flag(ctrl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ctrl, LV_SCROLLBAR_MODE_OFF);

    /* Prev - izquierda */
    /* Prev - boton ancho */
    s_btn_prev = lv_btn_create(ctrl);
    lv_obj_set_size(s_btn_prev, 120, 52);
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_make(0x22,0x22,0x2E), 0);
    lv_obj_set_style_bg_color(s_btn_prev, lv_color_make(0x7C,0x5C,0xFC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_btn_prev, 26, 0);
    lv_obj_set_style_shadow_width(s_btn_prev, 0, 0);
    lv_obj_set_style_outline_width(s_btn_prev, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(s_btn_prev, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_prev, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_btn_prev, lv_color_make(0x44,0x44,0x55), 0);
    lv_obj_set_style_border_width(s_btn_prev, 1, 0);
    lv_obj_align(s_btn_prev, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_t *lprev = lv_label_create(s_btn_prev);
    lv_label_set_text(lprev, LV_SYMBOL_PREV "  Prev");
    lv_obj_set_style_text_font(lprev, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lprev, lv_color_make(0xCC,0xCC,0xCC), 0);
    lv_obj_center(lprev);
    lv_obj_add_event_cb(s_btn_prev, prev_cb, LV_EVENT_CLICKED, NULL);
    /* Play - centro */
    s_btn_play = lv_btn_create(ctrl);
    lv_obj_set_size(s_btn_play, 80, 80);
    lv_obj_set_style_bg_color(s_btn_play, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_set_style_bg_color(s_btn_play, lv_color_make(0x60,0x45,0xCC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_btn_play, 40, 0);
    lv_obj_set_style_shadow_width(s_btn_play, 0, 0);
    lv_obj_set_style_outline_width(s_btn_play, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(s_btn_play, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_play, 0, LV_STATE_FOCUSED);
    lv_obj_align(s_btn_play, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *lplay = lv_label_create(s_btn_play);
    lv_label_set_text(lplay, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(lplay, &lv_font_montserrat_14, 0);
    lv_obj_center(lplay);
    lv_obj_add_event_cb(s_btn_play, play_cb, LV_EVENT_CLICKED, NULL);

    /* Next - derecha */
    /* Next - boton ancho */
    s_btn_next = lv_btn_create(ctrl);
    lv_obj_set_size(s_btn_next, 120, 52);
    lv_obj_set_style_bg_color(s_btn_next, lv_color_make(0x22,0x22,0x2E), 0);
    lv_obj_set_style_bg_color(s_btn_next, lv_color_make(0x7C,0x5C,0xFC), LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_btn_next, 26, 0);
    lv_obj_set_style_shadow_width(s_btn_next, 0, 0);
    lv_obj_set_style_outline_width(s_btn_next, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(s_btn_next, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_btn_next, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(s_btn_next, lv_color_make(0x44,0x44,0x55), 0);
    lv_obj_set_style_border_width(s_btn_next, 1, 0);
    lv_obj_align(s_btn_next, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_t *lnext = lv_label_create(s_btn_next);
    lv_label_set_text(lnext, "Next  " LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(lnext, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lnext, lv_color_make(0xCC,0xCC,0xCC), 0);
    lv_obj_center(lnext);
    lv_obj_add_event_cb(s_btn_next, next_cb, LV_EVENT_CLICKED, NULL);

    /* ������ Volume controls ������ */
    lv_obj_t *vol_row = lv_obj_create(left);
    lv_obj_set_size(vol_row, 380, 48);
    lv_obj_align(vol_row, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(vol_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vol_row, 0, 0);
    lv_obj_set_style_pad_all(vol_row, 0, 0);
    lv_obj_clear_flag(vol_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(vol_row, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *vdn = lv_btn_create(vol_row);
    lv_obj_set_size(vdn, 56, 40);
    lv_obj_align(vdn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(vdn, lv_color_make(0x2A,0x2A,0x3A), 0);
    lv_obj_set_style_radius(vdn, 8, 0);
    lv_obj_set_style_shadow_width(vdn, 0, 0);
    lv_obj_set_style_outline_width(vdn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(vdn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(vdn, 0, LV_STATE_FOCUSED);
    lv_obj_t *vdnl = lv_label_create(vdn);
    lv_label_set_text(vdnl, LV_SYMBOL_MINUS);
    lv_obj_center(vdnl);
    lv_obj_add_event_cb(vdn, vol_down_cb, LV_EVENT_CLICKED, NULL);

    s_vol_label = lv_label_create(vol_row);
    lv_label_set_text(s_vol_label, LV_SYMBOL_VOLUME_MID "  50%");
    lv_obj_set_style_text_color(s_vol_label, lv_color_make(0x88,0x88,0xAA), 0);
    lv_obj_set_style_text_font(s_vol_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_vol_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *vup = lv_btn_create(vol_row);
    lv_obj_set_size(vup, 56, 40);
    lv_obj_align(vup, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(vup, lv_color_make(0x2A,0x2A,0x3A), 0);
    lv_obj_set_style_radius(vup, 8, 0);
    lv_obj_set_style_shadow_width(vup, 0, 0);
    lv_obj_set_style_outline_width(vup, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(vup, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(vup, 0, LV_STATE_FOCUSED);
    lv_obj_t *vupl = lv_label_create(vup);
    lv_label_set_text(vupl, LV_SYMBOL_PLUS);
    lv_obj_center(vupl);
    lv_obj_add_event_cb(vup, vol_up_cb, LV_EVENT_CLICKED, NULL);

    /* Mute button */
    s_mute_btn = lv_btn_create(vol_row);
    lv_obj_set_size(s_mute_btn, 56, 40);
    lv_obj_align(s_mute_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_mute_btn, lv_color_make(0x2A,0x2A,0x3A), 0);
    lv_obj_set_style_radius(s_mute_btn, 8, 0);
    lv_obj_set_style_shadow_width(s_mute_btn, 0, 0);
    lv_obj_set_style_outline_width(s_mute_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(s_mute_btn, 0, LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(s_mute_btn, 0, LV_STATE_FOCUSED);
    lv_obj_t *mbl = lv_label_create(s_mute_btn);
    lv_label_set_text(mbl, LV_SYMBOL_VOLUME_MID);
    lv_obj_center(mbl);
    lv_obj_add_event_cb(s_mute_btn, mute_cb, LV_EVENT_CLICKED, NULL);

    /* RIGHT PANEL - Grid playlists */
    lv_obj_t *right = lv_obj_create(s_screen);
    lv_obj_set_size(right, 530, 500);
    lv_obj_align(right, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(right, lv_color_make(0x12,0x12,0x18), 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_radius(right, 16, 0);
    lv_obj_set_style_pad_all(right, 4, 0);
    lv_obj_t *qlbl = lv_label_create(right);
    lv_label_set_text(qlbl, LV_SYMBOL_LIST "  Mis Playlists");
    lv_obj_set_style_text_color(qlbl, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_set_style_text_font(qlbl, &lv_font_montserrat_14, 0);
    lv_obj_align(qlbl, LV_ALIGN_TOP_LEFT, 8, 8);
    s_playlist = lv_list_create(right);
    lv_obj_set_size(s_playlist, 514, 456);
    lv_obj_align(s_playlist, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(s_playlist, lv_color_make(0x12,0x12,0x18), 0);
    lv_obj_set_style_bg_color(s_playlist, lv_color_make(0x12,0x12,0x18), LV_PART_SCROLLBAR);
    lv_obj_set_style_border_width(s_playlist, 0, 0);
    lv_obj_set_scrollbar_mode(s_playlist, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_flag(s_playlist, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_playlist, LV_DIR_VER);

    /* Timer para progreso */
    lv_timer_create(progress_timer_cb, 1000, NULL);

    return s_screen;
}

void screen_music_set_volume(int vol)
{
    s_volume = vol;
    s_muted = false;
    bsp_display_lock(0);
    if (s_vol_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", vol);
        lv_label_set_text(s_vol_label, buf);
    }
    if (s_mute_btn) {
        lv_obj_set_style_bg_color(s_mute_btn, lv_color_make(0x2A,0x2A,0x3A), 0);
        lv_obj_t *lbl = lv_obj_get_child(s_mute_btn, 0);
        if (lbl) lv_label_set_text(lbl, LV_SYMBOL_VOLUME_MID);
    }
    bsp_display_unlock();
}





void screen_music_reset_artwork(void) {
    if (!s_artwork) return;
    bsp_display_lock(0);
    lv_obj_clean(s_artwork);
    lv_obj_t *art_icon = lv_label_create(s_artwork);
    lv_label_set_text(art_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(art_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(art_icon, lv_color_make(0x7C,0x5C,0xFC), 0);
    lv_obj_center(art_icon);
    bsp_display_unlock();
    /* Forzar re-descarga */
    memset(s_image_url, 0, sizeof(s_image_url));
}

void screen_music_reset_playlists(void) {
    s_pl_count = 0;
    if (!s_playlist) return;
    bsp_display_lock(0);
    lv_obj_clean(s_playlist);
    bsp_display_unlock();
}


