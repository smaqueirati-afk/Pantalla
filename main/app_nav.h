/**
 * @file app_nav.h
 * @brief Capa de navegación entre pantallas — SM Domótica
 *
 * Uso desde tarea externa (MQTT, SNTP, etc.):
 *   app_nav_goto(SCREEN_SPOTIFY);          // thread-safe, adquiere lock
 *   app_nav_set_wifi(true);
 *   app_nav_set_datetime("Lun, 11...", "10:30:00");
 *
 * Desde callbacks LVGL (ya dentro del lock): se usan internamente.
 */

#ifndef APP_NAV_H
#define APP_NAV_H

#include <stdbool.h>
#include <stdint.h>

/* ── Identificadores de pantalla ─────────────────────────── */
typedef enum {
    SCREEN_MAIN_MENU = 0,
    SCREEN_SPOTIFY,
    SCREEN_LUCES,
    SCREEN_PORTON,
    SCREEN_SETTINGS,
    SCREEN_SONIDO,
    SCREEN_COUNT          /* siempre al final */
} app_screen_t;

/* ── API pública ─────────────────────────────────────────── */

/**
 * @brief Inicializa la navegación y muestra el menú principal.
 *        Llamar una sola vez, dentro del bloque lvgl_port_lock().
 */
void app_nav_init(void);

/**
 * @brief Navega a una pantalla.
 *        Thread-safe: adquiere lvgl_port_lock internamente.
 *        NO llamar desde dentro de un callback LVGL (deadlock).
 */
void app_nav_goto(app_screen_t screen);

/** Retorna la pantalla actualmente visible. */
app_screen_t app_nav_current(void);

/* ── Helpers thread-safe para actualizar status ──────────── */

/** Actualiza WiFi en el menú principal (thread-safe). */
void app_nav_set_wifi(bool connected);

/** Actualiza estado Home Assistant (thread-safe). */
void app_nav_set_ha(bool online);

/**
 * @brief Actualiza fecha y hora en el footer del menú.
 * @param date  Ej: "Lun, 11 de may del 2026"
 * @param time  Ej: "10:30:00"
 */
void app_nav_set_datetime(const char *date, const char *time);

/** Actualiza track de Spotify (thread-safe). */
void app_nav_spotify_set_track(const char *title, const char *artist);

/** Actualiza progreso de Spotify (thread-safe). */
void app_nav_spotify_set_progress(int32_t current_s, int32_t total_s);

/** Actualiza estado play/pause de Spotify (thread-safe). */
void app_nav_spotify_set_playing(bool playing);

#endif /* APP_NAV_H */
