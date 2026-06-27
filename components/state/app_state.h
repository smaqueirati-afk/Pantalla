#pragma once
/**
 * @file app_state.h
 * @brief Central application state — single source of truth.
 *
 * All tasks read state via app_state_get() (returns a const pointer).
 * Mutations go through dedicated setters that also post the relevant event.
 */

#include <stdint.h>
#include <stdbool.h>
#include "state/event_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Audio output mode ──────────────────────────────────────────────────────*/
typedef enum {
    AUDIO_MODE_INTERNAL = 0,  /* ES8311 + NS4150B onboard speaker */
    AUDIO_MODE_BLUETOOTH,     /* A2DP → external BT speaker        */
    AUDIO_MODE_BOTH,          /* Internal + BT simultaneously       */
} audio_mode_t;

/* ── Playback state ─────────────────────────────────────────────────────────*/
typedef enum {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED,
} playback_state_t;

/* ── Track metadata ─────────────────────────────────────────────────────────*/
#define TRACK_TITLE_MAX   128
#define TRACK_ARTIST_MAX  64
#define TRACK_ALBUM_MAX   64
#define TRACK_ART_URL_MAX 256

typedef struct {
    char            title[TRACK_TITLE_MAX];
    char            artist[TRACK_ARTIST_MAX];
    char            album[TRACK_ALBUM_MAX];
    char            art_url[TRACK_ART_URL_MAX];
    uint32_t        duration_ms;
    uint32_t        position_ms;
    char            spotify_id[24];
} track_info_t;

/* ── Palette ─────────────────────────────────────────────────────────────────*/
typedef struct {
    uint32_t primary;    /* ARGB8888 — dominant colour     */
    uint32_t secondary;  /* ARGB8888 — second dominant     */
    uint32_t accent;     /* ARGB8888 — vibrant highlight   */
    uint32_t text_on;    /* ARGB8888 — readable text over primary */
} color_palette_t;

/* ── BT device ──────────────────────────────────────────────────────────────*/
#define BT_DEV_NAME_MAX 64
typedef struct {
    uint8_t  addr[6];
    char     name[BT_DEV_NAME_MAX];
    bool     connected;
    int8_t   rssi;
} bt_device_t;

/* ── App state (read-only via getter) ───────────────────────────────────────*/
typedef struct {
    /* Playback */
    playback_state_t playback;
    track_info_t     track;
    uint8_t          volume_pct;    /* 0–100 */
    uint8_t          volume_bt_pct; /* separate BT volume */

    /* Audio routing */
    audio_mode_t     audio_mode;

    /* Album art */
    void            *album_art_buf;   /* PSRAM pointer to decoded JPEG pixels */
    uint32_t         album_art_w;
    uint32_t         album_art_h;
    bool             album_art_ready;

    /* Dynamic palette */
    color_palette_t  palette;
    bool             palette_ready;

    /* BT */
    bt_device_t      bt_connected_dev;
    bool             bt_connected;

    /* WiFi */
    bool             wifi_connected;
    char             wifi_ssid[33];

    /* Spotify */
    bool             spotify_active;  /* we are the active Spotify Connect device */
} app_state_t;

/** Initialise state from NVS (call once before any task reads state). */
void app_state_init(void);

/** Get a const pointer to the current state. Never modify directly. */
const app_state_t *app_state_get(void);

/* ── Setters — post events automatically ───────────────────────────────────*/
void app_state_set_playback(playback_state_t state);
void app_state_set_track(const track_info_t *track);
void app_state_set_volume(uint8_t pct);
void app_state_set_volume_bt(uint8_t pct);
void app_state_set_audio_mode(audio_mode_t mode);
void app_state_set_album_art(void *buf, uint32_t w, uint32_t h);
void app_state_set_palette(const color_palette_t *palette);
void app_state_set_bt_connected(const bt_device_t *dev);
void app_state_set_bt_disconnected(void);
void app_state_set_wifi(bool connected, const char *ssid);
void app_state_set_progress(uint32_t position_ms);

#ifdef __cplusplus
}
#endif
