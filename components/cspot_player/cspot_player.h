#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cspot_player_init(const char *device_name);
esp_err_t cspot_player_start(void);

#ifdef __cplusplus
}
#endif
