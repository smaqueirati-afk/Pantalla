#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t snapcast_client_init(const char *host, uint16_t port);
esp_err_t snapcast_client_start(void);
void      snapcast_client_stop(void);
bool      snapcast_client_is_connected(void);

#ifdef __cplusplus
}
#endif
