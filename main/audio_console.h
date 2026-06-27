/**
 * @file audio_console.h
 * @brief Pantalla Audio Console — API pública
 */
#ifndef AUDIO_CONSOLE_H
#define AUDIO_CONSOLE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/** Crea la pantalla sobre el objeto padre. */
void audio_console_create(lv_obj_t *parent);

/** Setea valores de EQ desde código (rango -12 a +12 dB). */
void audio_console_set_eq(int16_t master_db, int16_t bass_db, int16_t treble_db);

/** Actualiza barras del espectro. bars[]: valores 0.0-1.0, n = cantidad. */
void audio_console_set_spectrum(float bars[], int n);

/** Actualiza VU meters. l, r: valores 0.0-1.0. */
void audio_console_set_vu(float l, float r);

/** Setea mute desde código. */
void audio_console_set_mute(bool muted);

bool    audio_console_get_mute(void);
int16_t audio_console_get_master(void);
int16_t audio_console_get_bass(void);
int16_t audio_console_get_treble(void);

/*
 * Callbacks weak — implementalos en tu app:
 *
 *   void on_audio_back(void);
 *   void on_audio_mute_changed(bool muted);
 *   void on_audio_eq_changed(int16_t master, int16_t bass, int16_t treble);
 */

#endif /* AUDIO_CONSOLE_H */
