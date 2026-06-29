#pragma once
#include <stdbool.h>
#include <stddef.h>

bool wifi_provision_has_credentials(void);
void wifi_provision_ota_c6(const char *image_url);
void wifi_provision_check_c6_version(void);
bool wifi_provision_get_credentials(char *ssid, size_t ssid_len,
                                    char *pass, size_t pass_len,
                                    char *token, size_t token_len);
void wifi_provision_start_portal(void);
bool wifi_provision_connect(void);
