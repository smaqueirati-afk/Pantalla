#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_feedback_init(void);
void      audio_beep_short(void);   /* click suave - botones generales */
void      audio_beep_on(void);      /* tono subiendo - encender luz */
void      audio_beep_off(void);     /* tono bajando - apagar luz */
void      audio_beep_porton(void);  /* beep doble - portón */

#ifdef __cplusplus
}
#endif

/* Devuelve el handle del codec para uso compartido */
esp_codec_dev_handle_t audio_feedback_get_codec(void);
esp_err_t audio_feedback_set_rate(int rate);

