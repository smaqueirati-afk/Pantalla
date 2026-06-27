#include "cspot_player.h"
extern "C" {
#include "audio_feedback.h"
extern "C" {
#include "driver/gpio.h"
}
#define PA_GPIO 53
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_http_server.h"
#include "mdns.h"
}
#include <CSpotContext.h>
#include <LoginBlob.h>
#include <SpircHandler.h>
#include <TrackPlayer.h>
#include <BellTask.h>
#include <BellUtils.h>
#include <MDNSService.h>
#include <CircularBuffer.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

static const char* TAG = "cspot_player";
static std::string s_device_name = "ESP32-P4 Domotica";
static bell::CircularBuffer* s_audioBuf = nullptr;
static std::atomic<bool> s_paused{false};
static std::atomic<bool> s_audioRunning{false};

// Shared between HTTP handlers and main task
static std::shared_ptr<cspot::LoginBlob> s_blob;
static std::atomic<bool> s_gotBlob{false};

static void audio_feed_task(void* arg) {
    s_audioRunning = true;
    std::vector<uint8_t> buf(4096);
    while (s_audioRunning) {
        if (!s_paused && s_audioBuf) {
            size_t rd = s_audioBuf->read(buf.data(), buf.size());
            if (rd > 0) {
                esp_codec_dev_handle_t codec = audio_feedback_get_codec();
                gpio_set_level((gpio_num_t)PA_GPIO, 1);
                if (codec) esp_codec_dev_write(codec, buf.data(), rd);
            } else vTaskDelay(pdMS_TO_TICKS(5));
        } else vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}

static esp_err_t spotify_info_get(httpd_req_t* req) {
    std::string info = s_blob->buildZeroconfInfo();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, info.c_str(), info.size());
    return ESP_OK;
}

static esp_err_t spotify_info_post(httpd_req_t* req) {
    std::string body(req->content_len, '\0');
    httpd_req_recv(req, body.data(), req->content_len);

    // Parse form URL encoded
    std::map<std::string, std::string> qmap;
    std::string key, val;
    bool inKey = true;
    auto decode = [](const std::string& s) {
        std::string r; r.reserve(s.size());
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '+') { r += ' '; }
            else if (s[i] == '%' && i+2 < s.size()) {
                int c = 0;
                sscanf(s.substr(i+1,2).c_str(), "%x", &c);
                r += (char)c; i += 2;
            } else r += s[i];
        }
        return r;
    };
    for (char c : body) {
        if (c == '=') { inKey = false; }
        else if (c == '&') { qmap[decode(key)] = decode(val); key.clear(); val.clear(); inKey = true; }
        else if (inKey) key += c;
        else val += c;
    }
    if (!key.empty()) qmap[decode(key)] = decode(val);

    s_blob->loadZeroconfQuery(qmap);
    s_gotBlob = true;

    const char* resp = "{\"status\":101,\"spotifyError\":0,\"statusString\":\"ERROR-OK\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

class CSpotMainTask : public bell::Task {
public:
    CSpotMainTask() : bell::Task("cspot_main", 32*1024, 5, 1) { startTask(); }
    void runTask() override {
        ESP_LOGI(TAG, "cspot starting: %s", s_device_name.c_str());
        mdns_init();
        mdns_hostname_set("esp32p4");

        bell::CircularBuffer audioBuf(512 * 1024);
        s_audioBuf = &audioBuf;
        audio_feedback_set_rate(44100);
        xTaskCreatePinnedToCore(audio_feed_task, "cspot_audio", 8192, NULL, 6, NULL, 1);

        s_blob = std::make_shared<cspot::LoginBlob>(s_device_name);
        s_gotBlob = false;

        // Start IDF HTTP server
        httpd_handle_t server = NULL;
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.server_port = 8082;
        cfg.ctrl_port = 32770;
        cfg.ctrl_port = 32769;
        cfg.uri_match_fn = httpd_uri_match_wildcard;
        esp_err_t httpd_err = httpd_start(&server, &cfg);
        ESP_LOGI(TAG, "httpd_start port=%d ctrl=%d err=%d", cfg.server_port, cfg.ctrl_port, httpd_err);

        httpd_uri_t get_uri = { "/spotify_info", HTTP_GET, spotify_info_get, NULL };
        httpd_uri_t post_uri = { "/spotify_info", HTTP_POST, spotify_info_post, NULL };
        httpd_register_uri_handler(server, &get_uri);
        httpd_register_uri_handler(server, &post_uri);

        // mDNS advertisement
        mdns_service_add(s_device_name.c_str(), "_spotify-connect", "_tcp", 8082, NULL, 0);
        mdns_service_txt_item_set("_spotify-connect", "_tcp", "VERSION", "1.0");
        mdns_service_txt_item_set("_spotify-connect", "_tcp", "CPath", "/spotify_info");
        mdns_service_txt_item_set("_spotify-connect", "_tcp", "Stack", "SP");

        ESP_LOGI(TAG, "Waiting '%s' on Spotify...", s_device_name.c_str());
        while (!s_gotBlob) { BELL_SLEEP_MS(500); }

        httpd_stop(server);

        std::vector<uint8_t> token;
        std::shared_ptr<cspot::Context> ctx;
        for (int attempt = 0; attempt < 5 && token.empty(); attempt++) {
            if (attempt > 0) {
                ESP_LOGW(TAG, "Retry connect %d/5 - waiting for network...", attempt+1);
            BELL_SLEEP_MS(10000);
            }
            ctx = cspot::Context::createFromBlob(s_blob);
            ctx->session->connectWithRandomAp();
            BELL_SLEEP_MS(500);
            token = ctx->session->authenticate(s_blob);
        }
        if (token.empty()) {
            ESP_LOGE(TAG, "Auth failed after retries");
            s_audioRunning = false;
            return;
        }

        ctx->session->startTask();
        ESP_LOGI(TAG, "Waiting for TX queue to drain...");
        BELL_SLEEP_MS(3000);
        auto handler = std::make_shared<cspot::SpircHandler>(ctx);

        handler->getTrackPlayer()->setDataCallback(
            [&audioBuf](uint8_t* data, size_t bytes, std::string_view) -> size_t {
                static int dbg_count = 0;
                if (++dbg_count % 100 == 1) ESP_LOGI("cspot_audio", "data cb: %d bytes (call #%d)", (int)bytes, dbg_count);
                size_t written = 0;
                while (written < bytes) {
                    size_t w = audioBuf.write(data+written, bytes-written);
                    if (w == 0) BELL_SLEEP_MS(5); else written += w;
                }
                return bytes;
            });

        handler->setEventHandler(
            [&audioBuf](std::unique_ptr<cspot::SpircHandler::Event> event) {
                switch (event->eventType) {
                    case cspot::SpircHandler::EventType::PLAY_PAUSE:
                        s_paused = std::get<bool>(event->data); break;
                    case cspot::SpircHandler::EventType::FLUSH:
                    case cspot::SpircHandler::EventType::SEEK:
                    case cspot::SpircHandler::EventType::PLAYBACK_START:
                        audioBuf.emptyBuffer(); break;
                    default: break;
                }
            });

        handler->subscribeToMercury();
        ESP_LOGI(TAG, "Spotify Connect ready!");
        while (true) ctx->session->handlePacket();
    }
};

static std::unique_ptr<CSpotMainTask> s_cspot_task;

extern "C" esp_err_t cspot_player_init(const char* device_name) {
    if (device_name && strlen(device_name) > 0) s_device_name = device_name;
    
    return ESP_OK;
}

extern "C" esp_err_t cspot_player_start(void) {
    if (!s_cspot_task) s_cspot_task = std::make_unique<CSpotMainTask>();
    return ESP_OK;
}























