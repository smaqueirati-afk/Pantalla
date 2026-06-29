/**
 * @file wifi_provision.c
 * @brief WiFi provisioning via portal captivo + IP estÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¡tica 192.168.1.150
 */

#include "wifi_provision.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_hosted.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_provision";

#define WIFI_SSID_KEY    "wifi_ssid"
#define WIFI_PASS_KEY    "wifi_pass"
#define HA_TOKEN_KEY     "ha_token"
#define NVS_NAMESPACE    "domotica"
#define AP_SSID          "Domotica-Setup"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     5

/* IP estÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¡tica en la red de casa */
#define STATIC_IP        "192.168.1.150"
#define STATIC_GW        "192.168.1.1"
#define STATIC_NETMASK   "255.255.255.0"

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static httpd_handle_t s_server = NULL;

static const char *PORTAL_HTML =
"<!DOCTYPE html><html><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Domotica Setup</title>"
"<style>"
"body{font-family:sans-serif;background:#101014;color:#fff;display:flex;"
"justify-content:center;align-items:center;min-height:100vh;margin:0}"
".card{background:#1e1e24;padding:2rem;border-radius:12px;width:90%;max-width:400px}"
"h1{color:#7c5cfc;margin-top:0}input{width:100%;padding:10px;margin:8px 0 16px;"
"background:#2a2a35;border:1px solid #444;border-radius:6px;color:#fff;box-sizing:border-box}"
"button{width:100%;padding:12px;background:#7c5cfc;color:#fff;border:none;"
"border-radius:6px;font-size:16px;cursor:pointer}"
".label{color:#aaa;font-size:14px}.info{color:#7c5cfc;font-size:13px;margin-bottom:1rem}"
"</style></head><body><div class='card'>"
"<h1>Domotica Setup</h1>"
"<div class='info'>IP fija: 192.168.1.150</div>"
"<form method='POST' action='/save'>"
"<div class='label'>Red WiFi (SSID)</div>"
"<input name='ssid' type='text' placeholder='MiRedWiFi' required>"
"<div class='label'>Contrasena WiFi</div>"
"<input name='pass' type='password' placeholder='contrasena'>"
"<div class='label'>Token Home Assistant</div>"
"<input name='token' type='text' placeholder='eyJ0...' required>"
"<button type='submit'>Guardar y conectar</button>"
"</form></div></body></html>";

static const char *SUCCESS_HTML =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<title>Guardado</title>"
"<style>body{font-family:sans-serif;background:#101014;color:#fff;"
"display:flex;justify-content:center;align-items:center;min-height:100vh}"
".card{background:#1e1e24;padding:2rem;border-radius:12px;text-align:center}"
"h1{color:#4caf50}.ip{color:#7c5cfc;font-size:1.2rem;margin:1rem 0}"
"</style></head><body><div class='card'>"
"<h1>Configuracion guardada</h1>"
"<div class='ip'>IP: 192.168.1.150</div>"
"<p>El dispositivo se reiniciara y conectara a tu red.</p>"
"<p>Podras acceder desde tu red en:<br><b>192.168.1.150</b></p>"
"</div></body></html>";

static void url_decode(char *dst, const char *src, size_t max_len)
{
    size_t i = 0, j = 0;
    while (src[i] && j < max_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = 0;
}

static bool get_form_field(const char *body, const char *key, char *out, size_t max_len)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= max_len) len = max_len - 1;
    char tmp[256] = {0};
    if (len < sizeof(tmp)) {
        memcpy(tmp, p, len);
        url_decode(out, tmp, max_len);
    }
    return true;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PORTAL_HTML, strlen(PORTAL_HTML));
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[512] = {0};
    int ret = httpd_req_recv(req, body, sizeof(body) - 1);
    if (ret <= 0) return ESP_FAIL;

    char ssid[64] = {0}, pass[64] = {0}, token[256] = {0};
    get_form_field(body, "ssid",  ssid,  sizeof(ssid));
    get_form_field(body, "pass",  pass,  sizeof(pass));
    get_form_field(body, "token", token, sizeof(token));

    ESP_LOGI(TAG, "Guardando ssid=%s", ssid);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, WIFI_SSID_KEY, ssid);
        nvs_set_str(nvs, WIFI_PASS_KEY, pass);
        nvs_set_str(nvs, HA_TOKEN_KEY,  token);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SUCCESS_HTML, strlen(SUCCESS_HTML));

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Desconectado, reason=%d", disc ? disc->reason : -1);
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Reintentando WiFi (%d/%d)", s_retry_num, WIFI_MAX_RETRY);
        } else {
            if (s_wifi_event_group)
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        if (s_wifi_event_group)
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_provision_has_credentials(void)
{
    nvs_handle_t nvs;
    char ssid[64] = {0};
    size_t len = sizeof(ssid);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_get_str(nvs, WIFI_SSID_KEY, ssid, &len);
    nvs_close(nvs);
    return (err == ESP_OK && strlen(ssid) > 0);
}

bool wifi_provision_get_credentials(char *ssid, size_t ssid_len,
                                    char *pass, size_t pass_len,
                                    char *token, size_t token_len)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    nvs_get_str(nvs, WIFI_SSID_KEY, ssid,  &ssid_len);
    nvs_get_str(nvs, WIFI_PASS_KEY, pass,  &pass_len);
    nvs_get_str(nvs, HA_TOKEN_KEY,  token, &token_len);
    nvs_close(nvs);
    return true;
}

void wifi_provision_start_portal(void)
{
    ESP_LOGI(TAG, "Portal captivo: %s", AP_SSID);
    { esp_err_t en = esp_netif_init(); if (en != ESP_OK && en != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "netif_init: %d", en); }
    { esp_err_t el = esp_event_loop_create_default(); if (el != ESP_OK && el != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "evt_loop: %d", el); }

    /* netif AP lo provee el stack wifi_remote del C6 - NO crear (igual que wifi_manager) */
    wifi_init_config_t cfg2 = WIFI_INIT_CONFIG_DEFAULT();
    { esp_err_t e = esp_wifi_init(&cfg2); if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e); }
    /* Crear netif AP DESPUES del init (con el C6 este es el orden que no crashea) */
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == NULL) {
        esp_netif_create_default_wifi_ap();
    }

    /* (init movido arriba) */
    /* (init movido arriba) */

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* DHCP server del AP: el celu necesita IP para no quedar en "conectando" */
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_ip_info_t ipinfo;
        IP4_ADDR(&ipinfo.ip,      192, 168, 4, 1);
        IP4_ADDR(&ipinfo.gw,      192, 168, 4, 1);
        IP4_ADDR(&ipinfo.netmask, 255, 255, 255, 0);
        esp_netif_set_ip_info(ap_netif, &ipinfo);
        esp_netif_dhcps_start(ap_netif);
        ESP_LOGI(TAG, "DHCP server del AP activo en 192.168.4.1");
    } else {
        ESP_LOGW(TAG, "No se encontro netif AP - DHCP no iniciado");
    }

    httpd_config_t server_cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_server, &server_cfg) == ESP_OK) {
        httpd_uri_t root = { .uri = "/",     .method = HTTP_GET,  .handler = root_get_handler };
        httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
        httpd_register_uri_handler(s_server, &root);
        httpd_register_uri_handler(s_server, &save);
        ESP_LOGI(TAG, "Portal en http://192.168.4.1");
    }
}

bool wifi_provision_connect(void)
{
    char ssid[64] = {0}, pass[64] = {0}, token[256] = {0};
    if (!wifi_provision_get_credentials(ssid, sizeof(ssid),
                                        pass, sizeof(pass),
                                        token, sizeof(token))) return false;

    ESP_LOGI(TAG, "Conectando a: %s", ssid);

    s_wifi_event_group = xEventGroupCreate();
    { esp_err_t ei = esp_netif_init(); if (ei != ESP_OK && ei != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "netif_init: %d", ei); }
    { esp_err_t ee = esp_event_loop_create_default(); if (ee != ESP_OK && ee != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "evt_loop: %d", ee); }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    { esp_err_t ew = esp_wifi_init(&cfg); if (ew != ESP_OK && ew != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(ew); }
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta(); /* CREAR netif STA en el host: con esp_wifi_remote lwIP+DHCP corren en el P4, no en el C6 */
    if (!sta_netif) ESP_LOGW(TAG, "create_default_wifi_sta devolvio NULL (ya existia?)");

    /* (init movido arriba) */
    /* (init movido arriba) */

    esp_event_handler_instance_t inst_any, inst_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, &inst_any);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, &inst_got_ip);

    /* DHCP: el router asigna la IP automaticamente */
    ESP_LOGI(TAG, "Usando DHCP (IP automatica)");

    wifi_config_t sta_config = {0};
    memcpy(sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid)-1);
    memcpy(sta_config.sta.password, pass, sizeof(sta_config.sta.password)-1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* QUICK WIN: desactivar power save - el PS puede romper el handshake de
       asociacion en el C6 (asocia pero no completa, sin disconnect, sin IP). */
    { esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
      ESP_LOGI(TAG, "esp_wifi_set_ps(NONE): %d", ps); }

    /* Forzar conexion directa: con el C6 el evento STA_START no siempre dispara */
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_err_t ce = esp_wifi_connect();
    ESP_LOGI(TAG, "esp_wifi_connect() directo: %d", ce);

    /* SONDEO DIRECTO DE IP: el C6 no entrega eventos GOT_IP de forma confiable,
       asi que preguntamos al netif cada segundo si ya tiene IP (hasta 20s). */
    esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_netif_ip_info_t ipinfo;
        if (netif_sta && esp_netif_get_ip_info(netif_sta, &ipinfo) == ESP_OK
            && ipinfo.ip.addr != 0) {
            ESP_LOGI(TAG, "CONECTADO! IP: " IPSTR, IP2STR(&ipinfo.ip));
            return true;
        }
        ESP_LOGI(TAG, "Esperando IP... (%d/20)", i + 1);
    }
    ESP_LOGW(TAG, "Timeout: no se obtuvo IP en 20s");
    return false;
}

/* ============================================================
 *  OTA del C6 por SDIO DIRECTO - sin WiFi, sin AP, sin HTTP.
 *  Lee el network_adapter.bin embebido en el firmware del P4 y
 *  lo manda al C6 en chunks por SDIO con las funciones RPC del
 *  esp_hosted. Es el camino que evita todos los crashes de netif.
 * ============================================================ */

/* Funciones RPC del esp_hosted (definidas en rpc_wrap.c, sin header publico) */
extern int rpc_ota_begin(void);
extern int rpc_ota_write(uint8_t* ota_data, uint32_t ota_data_len);
extern int rpc_ota_end(void);

/* Binario del C6 embebido en el firmware del P4 (ver CMakeLists) */
extern const uint8_t c6_bin_start[] asm("_binary_network_adapter_bin_start");
extern const uint8_t c6_bin_end[]   asm("_binary_network_adapter_bin_end");

#define OTA_CHUNK 1400

void wifi_provision_ota_c6(const char *image_url)
{
    (void)image_url; /* ya no se usa: el bin esta embebido */

    const uint8_t *p   = c6_bin_start;
    int total          = (int)(c6_bin_end - c6_bin_start);

    ESP_LOGW(TAG, "=== OTA C6 por SDIO directo ===");
    ESP_LOGW(TAG, "Firmware embebido: %d bytes", total);

    if (total <= 0) {
        ESP_LOGE(TAG, "Binario embebido vacio! Revisar el CMakeLists.");
        return;
    }

    /* CRITICO: levantar el transporte SDIO con el C6 ANTES del OTA.
       En esta version, lo que dispara el transporte es esp_wifi_init()
       (lo vimos en todos los logs: "Base transport is set-up").
       Inicializamos WiFi SIN crear netif ni AP (eso crasheaba). */
    ESP_LOGW(TAG, "Levantando transporte via esp_wifi_init...");
    { esp_err_t e = esp_netif_init();                if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "netif_init: %d", e); }
    { esp_err_t e = esp_event_loop_create_default(); if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "evt_loop: %d", e); }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    { esp_err_t e = esp_wifi_init(&cfg);             if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "wifi_init: %d", e); }
    /* Poner modo STA (no AP, para no necesitar netif) y arrancar: esto sube el transporte */
    { esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA); if (e != ESP_OK) ESP_LOGW(TAG, "set_mode: %d", e); }
    { esp_err_t e = esp_wifi_start();                 if (e != ESP_OK) ESP_LOGW(TAG, "wifi_start: %d", e); }

    ESP_LOGW(TAG, "Esperando 5s a que el transporte SDIO este listo...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGW(TAG, "Iniciando ota_begin (puede tardar)...");
    int ret = rpc_ota_begin();
    if (ret != 0) {
        ESP_LOGE(TAG, "rpc_ota_begin fallo: %d", ret);
        return;
    }
    ESP_LOGW(TAG, "ota_begin OK. Enviando firmware por SDIO...");

    int off = 0;
    int last_pct = -1;
    while (off < total) {
        int chunk = (total - off > OTA_CHUNK) ? OTA_CHUNK : (total - off);
        ret = rpc_ota_write((uint8_t *)(p + off), chunk);
        if (ret != 0) {
            ESP_LOGE(TAG, "rpc_ota_write fallo en offset %d: %d", off, ret);
            rpc_ota_end();
            return;
        }
        off += chunk;
        int pct = (off * 100) / total;
        if (pct != last_pct && pct % 5 == 0) {
            ESP_LOGW(TAG, "OTA C6: %d%% (%d/%d)", pct, off, total);
            last_pct = pct;
        }
    }

    ESP_LOGW(TAG, "Firmware enviado. Finalizando (ota_end)...");
    ret = rpc_ota_end();
    if (ret != 0) {
        ESP_LOGE(TAG, "rpc_ota_end fallo: %d", ret);
        return;
    }

    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "=== OTA C6 COMPLETADO OK! ===");
    ESP_LOGW(TAG, "El C6 se va a reiniciar con el firmware 1.4.7");
    ESP_LOGW(TAG, "Reiniciando el P4 en 8 segundos...");
    ESP_LOGW(TAG, "===========================================");
    vTaskDelay(pdMS_TO_TICKS(8000));
    esp_restart();
}

/* ============================================================
 *  Diagnostico: leer la version del firmware del C6
 * ============================================================ */
void wifi_provision_check_c6_version(void)
{
    ESP_LOGW(TAG, "=== Chequeando version del C6 ===");

    /* Levantar transporte (igual que en el OTA) */
    { esp_err_t e = esp_netif_init();                if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "netif_init: %d", e); }
    { esp_err_t e = esp_event_loop_create_default(); if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "evt_loop: %d", e); }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    { esp_err_t e = esp_wifi_init(&cfg);             if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "wifi_init: %d", e); }
    { esp_err_t e = esp_wifi_set_mode(WIFI_MODE_STA); if (e != ESP_OK) ESP_LOGW(TAG, "set_mode: %d", e); }
    { esp_err_t e = esp_wifi_start();                 if (e != ESP_OK) ESP_LOGW(TAG, "wifi_start: %d", e); }

    vTaskDelay(pdMS_TO_TICKS(4000));

    esp_hosted_coprocessor_fwver_t ver;
    esp_err_t err = esp_hosted_get_coprocessor_fwversion(&ver);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "###############################################");
        ESP_LOGW(TAG, "### VERSION DEL C6: %d.%d.%d ###", (int)ver.major1, (int)ver.minor1, (int)ver.patch1);
        ESP_LOGW(TAG, "###############################################");
    } else {
        ESP_LOGE(TAG, "No se pudo leer la version del C6: %d", err);
    }
}

