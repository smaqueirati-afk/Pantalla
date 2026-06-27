/**
 * @file spotify_screen.h
 * @brief Pantalla Spotify — API pública
 */

#ifndef SPOTIFY_SCREEN_H
#define SPOTIFY_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/** Crea la pantalla completa sobre el objeto padre. */
void spotify_screen_create(lv_obj_t *parent);

/** Actualiza título y artista de la pista actual. */
void spotify_set_track(const char *title, const char *artist);

/** Actualiza barra de progreso. current_s y total_s en segundos. */
void spotify_set_progress(int32_t current_s, int32_t total_s);

/** Cambia ícono play/pause según estado. */
void spotify_set_playing(bool playing);

/** Carga la lista de playlists. n = cantidad (máx 8). */
void spotify_set_playlists(const char *names[], const char *counts[], int n);

/** Marca una playlist como activa por índice. */
void spotify_pl_set_active(int idx);

/*
 * Callbacks weak — implementalos en tu app:
 *
 *   void on_spotify_back(void);       // botón Inicio
 *   void on_spotify_sound(void);      // botón Sonido
 *   void on_spotify_prev(void);       // pista anterior
 *   void on_spotify_play(void);       // play / pause
 *   void on_spotify_next(void);       // pista siguiente
 *   void on_spotify_shuffle(void);    // aleatorio
 *   void on_spotify_repeat(void);     // repetir
 *   void on_spotify_pl_select(int);   // playlist seleccionada
 */

#endif /* SPOTIFY_SCREEN_H */
