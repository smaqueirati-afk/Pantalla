/**
 * @file snapcast_client.c
 * @brief Cliente Snapcast protocolo binario TCP v2
 * Conecta a Snapcast server (Music Assistant) puerto 1704
 * Usa codec ES8311 ya inicializado via audio_feedback_get_codec()
 */

#include "snapcast_client.h"
#include "audio_feedback.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_codec_dev.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

static const char *TAG = "snapcast";
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

/* ── Snapcast message types ── */
#define MSG_BASE          0
#define MSG_CODEC_HEADER  1
#define MSG_WIRE_CHUNK    2
#define MSG_SERVER_SETTINGS 3
#define MSG_TIME          4
#define MSG_HELLO         5
#define MSG_STREAM_TAGS   6

/* ── Snapcast wire header (26 bytes) ── */
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t id;
    uint16_t ref_id;
    uint32_t sent_sec;
    uint32_t sent_usec;
    uint32_t size;
} snap_hdr_t;

static char     s_host[64]  = {0};
static uint16_t s_port      = 1704;
static bool     s_running   = false;
static bool     s_connected = false;
static TaskHandle_t s_task  = NULL;

/* ── Send Snapcast Hello ── */
static void send_hello(int sock)
{
    const char *hello_json =
        "{\"Arch\":\"xtensa\","
        "\"ClientName\":\"Snapclient\","
        "\"HostName\":\"esp32p4\","
        "\"ID\":\"80:f1:b2:d3:2a:30\","
        "\"Instance\":1,"
        "\"MAC\":\"80:f1:b2:d3:2a:30\","
        "\"OS\":\"Linux x86_64\","
        "\"SnapStreamProtocolVersion\":2,"
        "\"Version\":\"0.27.0\"}";

    uint32_t json_len = strlen(hello_json);
    snap_hdr_t hdr = {
        .type    = MSG_HELLO,
        .id      = 0,
        .ref_id  = 0,
        .sent_sec  = 0,
        .sent_usec = 0,
        .size    = json_len,
    };
    send(sock, &hdr, sizeof(hdr), 0);
    send(sock, hello_json, json_len, 0);
    ESP_LOGI(TAG, "Hello enviado");
}

/* ── Send Time message ── */
static void send_time(int sock, uint16_t ref_id)
{
    const char *time_json = "{\"latency\":0}";
    uint32_t json_len = strlen(time_json);
    snap_hdr_t hdr = {
        .type    = MSG_TIME,
        .id      = 1,
        .ref_id  = ref_id,
        .sent_sec  = 0,
        .sent_usec = 0,
        .size    = json_len,
    };
    send(sock, &hdr, sizeof(hdr), 0);
    send(sock, time_json, json_len, 0);
}

/* ── Receive exact bytes ── */
static int recv_all(int sock, uint8_t *buf, int len)
{
    int total = 0;
    while (total < len) {
        int n = recv(sock, buf + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

/* ── Main task ── */
static void snapcast_task(void *arg)
{
    esp_codec_dev_handle_t codec = audio_feedback_get_codec();
    int sample_rate = 48000;
    int channels    = 2;
    int bits        = 16;

    while (s_running) {
        /* Connect */
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", s_port);

        ESP_LOGI(TAG, "Conectando a %s:%d", s_host, s_port);

        if (getaddrinfo(s_host, port_str, &hints, &res) != 0 || !res) {
            ESP_LOGW(TAG, "DNS fail — reintentando en 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { freeaddrinfo(res); vTaskDelay(pdMS_TO_TICKS(5000)); continue; }

        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &(struct timeval){.tv_sec=30}, sizeof(struct timeval));

        if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGW(TAG, "Conexion fallida — reintentando en 5s");
            close(sock); freeaddrinfo(res);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        freeaddrinfo(res);
        s_connected = true;
        ESP_LOGI(TAG, "Conectado al servidor Snapcast");

        /* Cliente inicia con Hello */
        send_hello(sock);

        /* Message loop */
        snap_hdr_t hdr;
        uint8_t *payload = NULL;
        uint32_t payload_cap = 0;

        while (s_running) {
            int rr = recv_all(sock, (uint8_t*)&hdr, sizeof(hdr));
            if (rr <= 0) {
                ESP_LOGW(TAG, "recv hdr falló: %d errno=%d", rr, errno);
                break;
            }
            ESP_LOGI(TAG, "MSG type=%d id=%d size=%ld", hdr.type, hdr.id, hdr.size);

            if (hdr.size > 0) {
                if (hdr.size > payload_cap) {
                    free(payload);
                    payload_cap = hdr.size + 512;
                    payload = malloc(payload_cap);
                    if (!payload) { ESP_LOGE(TAG, "OOM"); break; }
                }
                if (recv_all(sock, payload, hdr.size) <= 0) break;
            }

            switch (hdr.type) {
            case MSG_SERVER_SETTINGS: {
                /* Parse sample rate from JSON */
                char *sr = strstr((char*)payload, "\"bufferMs\"");
                ESP_LOGI(TAG, "ServerSettings recibido: %.*s", (int)MIN(hdr.size,200), (char*)payload);
                break;
            }
            case MSG_CODEC_HEADER: {
                /* PCM header: 4 bytes rate + 2 bytes bits + 1 byte channels */
                if (hdr.size >= 7) {
                    uint32_t rate; uint16_t b; uint8_t ch;
                    memcpy(&rate, payload,   4);
                    memcpy(&b,    payload+4, 2);
                    memcpy(&ch,   payload+6, 1);
                    sample_rate = rate; bits = b; channels = ch;
                    ESP_LOGI(TAG, "Codec: %dHz %dbit %dch", sample_rate, bits, channels);
                    /* Configure codec */
                    if (codec) {
                        esp_codec_dev_sample_info_t info = {
                            .sample_rate = sample_rate,
                            .channel     = channels,
                            .bits_per_sample = bits,
                        };
                        esp_codec_dev_close(codec);
                        esp_codec_dev_open(codec, &info);
                        esp_codec_dev_set_out_vol(codec, 80);
                    }
                }
                break;
            }
            case MSG_WIRE_CHUNK: {
                /* Audio chunk: 4 bytes timestamp sec + 4 bytes usec + PCM data */
                if (hdr.size > 8 && codec) {
                    uint8_t *pcm = payload + 8;
                    uint32_t pcm_len = hdr.size - 8;
                    esp_codec_dev_write(codec, pcm, pcm_len);
                }
                break;
            }
            case MSG_TIME:
                send_time(sock, hdr.id);
                break;
            case MSG_BASE:
            case MSG_STREAM_TAGS:
            default:
                ESP_LOGD(TAG, "Msg type %d size %ld", hdr.type, hdr.size);
                break;
            }
        }

        free(payload); payload = NULL; payload_cap = 0;
        close(sock);
        s_connected = false;
        ESP_LOGW(TAG, "Desconectado — reintentando en 5s");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelete(NULL);
}

esp_err_t snapcast_client_init(const char *host, uint16_t port)
{
    strncpy(s_host, host, sizeof(s_host)-1);
    s_port = port;
    return ESP_OK;
}

esp_err_t snapcast_client_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    xTaskCreate(snapcast_task, "snapcast", 8192, NULL, 5, &s_task);
    return ESP_OK;
}

void snapcast_client_stop(void)
{
    s_running = false;
    s_connected = false;
}

bool snapcast_client_is_connected(void)
{
    return s_connected;
}
