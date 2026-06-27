/**
 * @file luces_screen.h
 * @brief Pantalla Luces de la Oficina — API pública
 */

#ifndef LUCES_SCREEN_H
#define LUCES_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>

/** Crea la pantalla sobre el objeto padre. */
void luces_screen_create(lv_obj_t *parent);

/** Setea el estado de una luz desde código (ej: desde HA).
 *  @param idx   0=Tubo LED, 1=Dicroicas, 2=Escritorio,
 *               3=Tira LED, 4=Luz Abajo, 5=Luz Medio
 *  @param state true=encendida
 */
void luces_screen_set_light(int idx, bool state);

/** Retorna el estado actual de una luz. */
bool luces_screen_get_state(int idx);

/*
 * Callbacks weak — implementalos en tu app:
 *
 *   void on_luces_back(void);                    // botón Atrás
 *   void on_luz_changed(int idx, bool state);    // luz tocada
 */

#endif /* LUCES_SCREEN_H */
