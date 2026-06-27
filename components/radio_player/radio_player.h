#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void      radio_player_preinit(void);
esp_err_t radio_player_init(esp_codec_dev_handle_t spk_dev);
esp_err_t radio_player_play(const char *url);
esp_err_t radio_player_stop(void);
bool      radio_player_is_playing(void);
void      radio_player_set_vol(int vol);

#ifdef __cplusplus
}
#endif
