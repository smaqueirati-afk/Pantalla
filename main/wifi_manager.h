#pragma once
#include <stdbool.h>

void wifi_manager_start(void);
bool wifi_manager_is_connected(void);
void wifi_manager_get_ha_url(char *buf, int len);
void wifi_manager_get_ha_token(char *buf, int len);
