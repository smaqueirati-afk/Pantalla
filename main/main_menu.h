/**
 * @file main_menu.h
 * @brief Pantalla Principal — SM Soluciones Informaticas
 */

#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include "lvgl.h"
#include <stdbool.h>

/** Crea la pantalla completa sobre el objeto padre. */
void main_menu_create(lv_obj_t *parent);

/** Tick manual de animación — no necesario si usás el timer interno. */
void main_menu_tick(void);

/** Actualiza indicador WiFi. */
void main_menu_set_wifi(bool connected);

/** Actualiza indicador Home Assistant. */
void main_menu_set_ha(bool online);

/** Actualiza fecha y hora en el footer.
 *  @param date  "Vie, 8 de may del 2026"
 *  @param time  "05:45:33"
 */
void main_menu_set_datetime(const char *date, const char *time);

/** Pausa la animación de fondo (al salir de la pantalla). */
void main_menu_pause(void);

/** Reanuda la animación de fondo (al volver a la pantalla). */
void main_menu_resume(void);

/*
 * Callbacks weak — implementalos en tu app:
 *
 *   void on_menu_spotify(void);    // navegar a Spotify
 *   void on_menu_luces(void);      // navegar a Luces
 *   void on_menu_porton(void);     // navegar a Portón
 *   void on_menu_settings(void);   // navegar a Settings
 *   void on_menu_refresh(void);    // refrescar estado
 */

#endif /* MAIN_MENU_H */
