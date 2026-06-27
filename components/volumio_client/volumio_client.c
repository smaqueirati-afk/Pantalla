#include "volumio_client.h"
#include "screen_music.h"
#include "ma_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "volumio";
static SemaphoreHandle_t s_http_mutex = NULL;
static char s_buf[4096];
static int  s_buf_len = 0;
static char s_pl_buf[8192] = {0};
static int  s_pl_buf_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int copy = evt->data_len;
        if (s_buf_len + copy >= (int)sizeof(s_buf)-1)
            copy = sizeof(s_buf)-1 - s_buf_len;
        memcpy(s_buf + s_buf_len, evt->data, copy);
        s_buf_len += copy;
        s_buf[s_buf_len] = 0;
    }
    return ESP_OK;
}

static esp_err_t pl_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        int copy = evt->data_len;
        if (s_pl_buf_len + copy >= (int)sizeof(s_pl_buf)-1)
            copy = sizeof(s_pl_buf)-1 - s_pl_buf_len;
        memcpy(s_pl_buf + s_pl_buf_len, evt->data, copy);
        s_pl_buf_len += copy;
        s_pl_buf[s_pl_buf_len] = 0;
    }
    return ESP_OK;
}

static void volumio_get(const char *path) {
    if (s_http_mutex && xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(8000)) != pdTRUE) {
        ESP_LOGW(TAG, "HTTP mutex timeout: %s", path);
        return;
    }
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", VOLUMIO_HOST, VOLUMIO_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 2000,
        .disable_auto_redirect = true,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,
        .crt_bundle_attach = NULL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    s_buf_len = 0;
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (s_http_mutex) xSemaphoreGive(s_http_mutex);
}

static void volumio_poll_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(15000));
    int fail_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        volumio_get("/api/state");
        if (s_buf_len == 0) {
            fail_count++;
            if (fail_count >= 3) {
                ESP_LOGW(TAG, "Pi no responde, esperando 60s...");
                vTaskDelay(pdMS_TO_TICKS(60000));
                fail_count = 0;
            }
            continue;
        }
        fail_count = 0;
        cJSON *j = cJSON_Parse(s_buf);
        if (j) {
            ma_player_t p = {0};
            cJSON *title  = cJSON_GetObjectItem(j, "title");
            cJSON *artist = cJSON_GetObjectItem(j, "artist");
            cJSON *album  = cJSON_GetObjectItem(j, "album");
            cJSON *status = cJSON_GetObjectItem(j, "status");
            cJSON *vol    = cJSON_GetObjectItem(j, "volume");
            cJSON *dur    = cJSON_GetObjectItem(j, "duration");
            cJSON *pos    = cJSON_GetObjectItem(j, "position");
            if (title  && title->valuestring)
                strncpy(p.current_item.title,  title->valuestring,  sizeof(p.current_item.title)-1);
            if (artist && artist->valuestring)
                strncpy(p.current_item.artist, artist->valuestring, sizeof(p.current_item.artist)-1);
            if (album  && album->valuestring)
                strncpy(p.current_item.album,  album->valuestring,  sizeof(p.current_item.album)-1);
            if (status && status->valuestring)
                p.playing = (strcmp(status->valuestring, "playing") == 0);
            if (vol && cJSON_IsNumber(vol))
                p.volume = (float)vol->valuedouble / 100.0f;
            if (dur && cJSON_IsNumber(dur))
                p.current_item.duration = (int)dur->valuedouble;
            if (pos && cJSON_IsNumber(pos))
                p.elapsed_time = (int)pos->valuedouble;
            strncpy(p.player_id, "raspotify", sizeof(p.player_id)-1);
            screen_music_update(&p);
            if (vol && cJSON_IsNumber(vol))
                screen_music_set_volume((int)(vol->valuedouble));
            ESP_LOGI(TAG, "%s - %s [%s]",
                p.current_item.artist, p.current_item.title,
                p.playing ? "playing" : "stopped");
            cJSON_Delete(j);
        }
    }
}

void volumio_client_start(void) {
    if (!s_http_mutex) s_http_mutex = xSemaphoreCreateMutex();
    /* Polling desactivado - datos vienen de HA WebSocket */
    ESP_LOGI(TAG, "Volumio client -> %s:%d", VOLUMIO_HOST, VOLUMIO_PORT);
}

void volumio_play_pause(void) { volumio_get("/api/play"); }
void volumio_next(void)       { volumio_get("/api/next"); }
void volumio_prev(void)       { volumio_get("/api/prev"); }

void volumio_set_volume(int delta) {
    char path[32];
    snprintf(path, sizeof(path), "/api/volume?delta=%d", delta);
    volumio_get(path);
}

void volumio_play_playlist(const char *uri) {
    char path[256];
    snprintf(path, sizeof(path), "/api/play_playlist?uri=%s", uri);
    volumio_get(path);
}

void volumio_get_path(const char *path) {
    volumio_get(path);
}

void volumio_eq_bass(int gain_db) {
    char path[64];
    snprintf(path, sizeof(path), "/api/eq/bass?gain=%d", gain_db);
    volumio_get(path);
}

void volumio_eq_treble(int gain_db) {
    char path[64];
    snprintf(path, sizeof(path), "/api/eq/treble?gain=%d", gain_db);
    volumio_get(path);
}

void volumio_get_status(char *out, int len) {
    volumio_get("/api/restart/status");
    if (s_buf_len > 0 && out) {
        strncpy(out, s_buf, len-1);
        out[len-1] = 0;
    }
}

void volumio_restart_services(void) {
    ESP_LOGI(TAG, "Reiniciando servicios Pi...");
    volumio_get("/api/restart");
}

void volumio_fetch_playlists(void) {
    ESP_LOGI(TAG, "Fetching playlists...");
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/playlists", VOLUMIO_HOST, VOLUMIO_PORT);
    if (s_http_mutex && xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout en fetch playlists");
        return;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = pl_event_handler,
        .timeout_ms = 10000,
        .disable_auto_redirect = true,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,
        .crt_bundle_attach = NULL,
    };
    s_pl_buf_len = 0;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (s_http_mutex) xSemaphoreGive(s_http_mutex);
    ESP_LOGI(TAG, "pl_buf_len=%d", s_pl_buf_len);
    if (s_pl_buf_len > 0) {
        cJSON *j = cJSON_Parse(s_pl_buf);
        if (j && cJSON_IsArray(j)) {
            int n = cJSON_GetArraySize(j);
            ESP_LOGI(TAG, "playlists=%d", n);
            for (int i = 0; i < n && i < 20; i++) {
                cJSON *item = cJSON_GetArrayItem(j, i);
                cJSON *name = cJSON_GetObjectItem(item, "name");
                cJSON *uri  = cJSON_GetObjectItem(item, "uri");
                if (name && uri && name->valuestring && uri->valuestring)
                    screen_music_add_playlist_item(name->valuestring, uri->valuestring);
            }
            cJSON_Delete(j);
        }
    }
}

