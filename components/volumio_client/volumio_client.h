#pragma once
#include <stdbool.h>

#define VOLUMIO_HOST "192.168.1.202"
#define VOLUMIO_PORT 8080

void volumio_client_start(void);
void volumio_play_pause(void);
void volumio_next(void);
void volumio_prev(void);
void volumio_set_volume(int vol);
void volumio_fetch_playlists(void);
void volumio_get_path(const char *path);
void volumio_eq_bass(int gain_db);
void volumio_eq_treble(int gain_db);
void volumio_get_status(char *out, int len);
void volumio_restart_services(void);
void volumio_play_playlist(const char *uri);






