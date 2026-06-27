/**
 * @file ui_main.c
 * @brief LVGL render task, screen navigation and idle screensaver timer.
 *
 * Task: pinned to core 1, priority 5, 32KB stack.
 * The LVGL timer handler runs every 5ms (200Hz tick) — LVGL schedules
 * its own 60fps animation flush internally via esp_lvgl_port.
 *
 * Screen navigation uses lv_scr_load_anim() with SLIDE_LEFT/RIGHT.
 * Idle screensaver activates after IDLE_TIMEOUT_MS of no touch input.
 */

#include "ui/ui_main.h"
#include "ui/screen_now_playing.h"
#include "ui/screen_screensaver.h"
#include "bsp/bsp_display.h"
#include "state/event_bus.h"
#include "state/app_state.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "ui_main";

#define UI_TASK_STACK    (32 * 1024)
#define UI_TASK_PRIO     5
#define UI_TASK_CORE     1
#define IDLE_TIMEOUT_MS  (60 * 1000)  /* 60s → screensaver */

/* ── Module-level state ─────────────────────────────────────────────────────*/
static screen_id_t      s_current = SCREEN_NOW_PLAYING;
static screen_id_t      s_requested = SCREEN_NOW_PLAYING;
static bool             s_nav_pending = false;

static lv_obj_t        *s_screens[6] = {NULL};   /* one per screen_id_t */
static esp_timer_handle_t s_idle_timer = NULL;

static event_sub_t      s_event_queue = NULL;

/* ── Forward declarations ───────────────────────────────────────────────────*/
static lv_obj_t *screen_create(screen_id_t id);
static void      ui_task(void *arg);
static void      idle_timer_cb(void *arg);
static void      process_events(void);
static void      apply_global_theme(void);

/* ── Global dark theme colours (updated from palette events) ────────────────*/
lv_color_t g_col_bg      = {0};   /* near-black base          */
lv_color_t g_col_primary = {0};   /* dominant from album art  */
lv_color_t g_col_accent  = {0};   /* vibrant highlight        */
lv_color_t g_col_text    = {0};   /* primary text             */
lv_color_t g_col_text2   = {0};   /* secondary text           */

/* ── Public API ─────────────────────────────────────────────────────────────*/

void ui_start(void)
{
    s_event_queue = event_bus_subscribe(32);

    xTaskCreatePinnedToCore(
        ui_task, "ui_task",
        UI_TASK_STACK, NULL,
        UI_TASK_PRIO, NULL,
        UI_TASK_CORE);
}

void ui_navigate(screen_id_t screen)
{
    /* Safe to call from any task — navigation deferred to ui_task */
    s_requested  = screen;
    s_nav_pending = true;
}

screen_id_t ui_current_screen(void) { return s_current; }

/* ── Idle timer ─────────────────────────────────────────────────────────────*/

static void idle_timer_reset(void)
{
    esp_timer_stop(s_idle_timer);
    esp_timer_start_once(s_idle_timer, (uint64_t)IDLE_TIMEOUT_MS * 1000);
}

static void idle_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Idle timeout — activating screensaver");
    ui_navigate(SCREEN_SCREENSAVER);
}

/* ── Theme defaults ─────────────────────────────────────────────────────────*/
static void apply_global_theme(void)
{
    /* Default dark palette — overridden once album art palette arrives */
    g_col_bg      = lv_color_make(0x10, 0x10, 0x14);
    g_col_primary = lv_color_make(0x1D, 0xB9, 0x54);  /* Spotify green placeholder */
    g_col_accent  = lv_color_make(0x1D, 0xB9, 0x54);
    g_col_text    = lv_color_make(0xFF, 0xFF, 0xFF);
    g_col_text2   = lv_color_make(0xB3, 0xB3, 0xB3);

    /* Apply to LVGL default display background */
    lv_obj_set_style_bg_color(lv_scr_act(), g_col_bg, 0);
}

/* ── Screen factory ─────────────────────────────────────────────────────────*/
static lv_obj_t *screen_create(screen_id_t id)
{
    switch (id) {
        case SCREEN_NOW_PLAYING:  return screen_now_playing_create();
        case SCREEN_SCREENSAVER:  return screen_screensaver_create();
        /* Fase 2+ screens — stubs for now */
        default:
            ESP_LOGW(TAG, "Screen %d not yet implemented — returning blank", id);
            lv_obj_t *blank = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(blank, g_col_bg, 0);
            return blank;
    }
}

/* ── Event handling ─────────────────────────────────────────────────────────*/
static void process_events(void)
{
    app_event_t evt;
    while (xQueueReceive(s_event_queue, &evt, 0) == pdTRUE) {
        switch (evt.id) {
            case EVT_PALETTE_READY: {
                const app_state_t *st = app_state_get();
                /* Convert ARGB8888 → lv_color_t */
                uint32_t p  = st->palette.primary;
                uint32_t ac = st->palette.accent;
                uint32_t tx = st->palette.text_on;
                g_col_primary = lv_color_make((p>>16)&0xFF,(p>>8)&0xFF,p&0xFF);
                g_col_accent  = lv_color_make((ac>>16)&0xFF,(ac>>8)&0xFF,ac&0xFF);
                g_col_text    = lv_color_make((tx>>16)&0xFF,(tx>>8)&0xFF,tx&0xFF);
                /* Notify now-playing screen to re-apply colours */
                screen_now_playing_on_palette_changed();
                break;
            }
            case EVT_TRACK_CHANGED:
                screen_now_playing_on_track_changed();
                idle_timer_reset();
                break;
            case EVT_PLAYBACK_STATE:
                screen_now_playing_on_playback_state((playback_state_t)evt.data.u32);
                break;
            case EVT_PROGRESS_UPDATE:
                screen_now_playing_on_progress(evt.data.u32);
                break;
            case EVT_ALBUM_ART_READY:
                screen_now_playing_on_album_art_ready();
                break;
            case EVT_UI_TOUCH_ACTIVE:
                if (s_current == SCREEN_SCREENSAVER) {
                    ui_navigate(SCREEN_NOW_PLAYING);
                }
                idle_timer_reset();
                break;
            default:
                break;
        }
    }
}

/* ── LVGL task ──────────────────────────────────────────────────────────────*/
static void ui_task(void *arg)
{
    ESP_LOGI(TAG, "UI task started on core %d", xPortGetCoreID());

    /* Acquire LVGL lock for setup */
    bsp_lvgl_lock(portMAX_DELAY);

    apply_global_theme();

    /* Create initial screen */
    s_screens[SCREEN_NOW_PLAYING] = screen_create(SCREEN_NOW_PLAYING);
    lv_scr_load(s_screens[SCREEN_NOW_PLAYING]);
    s_current = SCREEN_NOW_PLAYING;

    /* Idle screensaver timer */
    esp_timer_create_args_t timer_cfg = {
        .callback = idle_timer_cb,
        .name     = "idle_timer",
    };
    esp_timer_create(&timer_cfg, &s_idle_timer);
    idle_timer_reset();

    bsp_lvgl_unlock();

    ESP_LOGI(TAG, "UI ready — now playing screen loaded");

    /* Main render loop — esp_lvgl_port drives the LVGL timer internally,
     * we just handle events and navigation requests here. */
    while (1) {
        /* Handle screen navigation */
        if (s_nav_pending) {
            s_nav_pending = false;
            screen_id_t target = s_requested;

            bsp_lvgl_lock(portMAX_DELAY);

            /* Create target screen if first visit */
            if (s_screens[target] == NULL) {
                s_screens[target] = screen_create(target);
            }

            /* Direction: screensaver fades, others slide left */
            lv_scr_load_anim_t anim_type =
                (target == SCREEN_SCREENSAVER) ? LV_SCR_LOAD_ANIM_FADE_ON
                                               : LV_SCR_LOAD_ANIM_MOVE_LEFT;
            lv_scr_load_anim(s_screens[target], anim_type, 300, 0, false);
            s_current = target;

            bsp_lvgl_unlock();
            ESP_LOGI(TAG, "Navigated to screen %d", target);
        }

        /* Process event bus */
        process_events();

        /* Yield — LVGL timer runs via esp_lvgl_port ISR/timer */
        vTaskDelay(pdMS_TO_TICKS(5));
        
    }
}
