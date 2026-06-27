#include "wifi_config_screen.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_system.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_cfg";
static lv_obj_t *ta_ssid = NULL;
static lv_obj_t *ta_pass = NULL;
static lv_obj_t *kb      = NULL;
static lv_obj_t *lbl_status = NULL;

static void save_and_reboot(const char *ssid, const char *pass) {
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_set_str(h, "ha_url",   "");
        nvs_set_str(h, "ha_token", "");
        nvs_commit(h); nvs_close(h);
    }
    lv_label_set_text(lbl_status, "Guardado! Reiniciando...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void btn_cb(lv_event_t *e) {
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);
    if (!ssid || strlen(ssid) == 0) {
        lv_label_set_text(lbl_status, "SSID no puede estar vacio");
        return;
    }
    save_and_reboot(ssid, pass);
}

static void ta_focus_cb(lv_event_t *e) {
    lv_keyboard_set_textarea(kb, lv_event_get_target(e));
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

void wifi_config_screen_show(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  Configurar WiFi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d4ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 700, 220);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l1 = lv_label_create(panel);
    lv_label_set_text(l1, "Red WiFi (SSID):");
    lv_obj_set_style_text_color(l1, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 20, 12);

    ta_ssid = lv_textarea_create(panel);
    lv_obj_set_size(ta_ssid, 650, 52);
    lv_obj_align(ta_ssid, LV_ALIGN_TOP_MID, 0, 38);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_placeholder_text(ta_ssid, "Nombre de tu red...");
    lv_obj_set_style_bg_color(ta_ssid, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_text_color(ta_ssid, lv_color_white(), 0);
    lv_obj_add_event_cb(ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *l2 = lv_label_create(panel);
    lv_label_set_text(l2, "Contrasena:");
    lv_obj_set_style_text_color(l2, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 20, 105);

    ta_pass = lv_textarea_create(panel);
    lv_obj_set_size(ta_pass, 650, 52);
    lv_obj_align(ta_pass, LV_ALIGN_TOP_MID, 0, 130);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_placeholder_text(ta_pass, "Contrasena...");
    lv_obj_set_style_bg_color(ta_pass, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_text_color(ta_pass, lv_color_white(), 0);
    lv_obj_add_event_cb(ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 300, 55);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 305);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x00d4ff), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_SAVE "  Guardar y Conectar");
    lv_obj_set_style_text_color(lb, lv_color_hex(0x000000), 0);
    lv_obj_center(lb);

    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, "");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xff6b6b), 0);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 370);

    kb = lv_keyboard_create(scr);
    /* Estilo legible para el teclado */
    static lv_style_t kb_style;
    lv_style_init(&kb_style);
    lv_style_set_bg_color(&kb_style, lv_color_hex(0x16213e));
    lv_style_set_text_color(&kb_style, lv_color_white());
    lv_style_set_text_font(&kb_style, &lv_font_montserrat_20);
    lv_style_set_border_color(&kb_style, lv_color_hex(0x00d4ff));
    lv_obj_add_style(kb, &kb_style, LV_PART_ITEMS);
    lv_obj_add_style(kb, &kb_style, 0);
    lv_obj_set_size(kb, 1024, 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_scr_load(scr);
}