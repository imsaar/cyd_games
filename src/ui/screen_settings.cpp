#include "screen_settings.h"
#include "ui_common.h"
#include "screen_manager.h"
#include "../hal/backlight.h"
#include "../hal/prefs.h"
#include "../hal/display.h"
#include <WiFi.h>
#include <Arduino.h>
#include <esp_ota_ops.h>

static lv_obj_t* lbl_info = nullptr;
static lv_obj_t* slider_bl = nullptr;
static lv_obj_t* lbl_bl_val = nullptr;
static lv_obj_t* sw_invert = nullptr;

static void update_info_text() {
    if (!lbl_info) return;

    const esp_partition_t* running = esp_ota_get_running_partition();

    char buf[320];
    snprintf(buf, sizeof(buf),
        "Hardware:  ESP32-2432S028 (CYD)\n"
        "HW ID:    %s\n"
        "Firmware: v%s\n"
        "Partition: %s\n"
        "\n"
        "IP:       %s\n"
        "SSID:     %s\n"
        "RSSI:     %d dBm\n"
        "Heap:     %lu / %lu KB\n"
        "Uptime:   %lu s\n"
        "\n"
        "OTA:      http://%s/update",
        WiFi.macAddress().c_str(),
        FW_VERSION,
        running ? running->label : "?",
        WiFi.localIP().toString().c_str(),
        WiFi.SSID().c_str(),
        WiFi.RSSI(),
        (unsigned long)(ESP.getFreeHeap() / 1024),
        (unsigned long)(ESP.getHeapSize() / 1024),
        (unsigned long)(millis() / 1000),
        WiFi.localIP().toString().c_str()
    );
    lv_label_set_text(lbl_info, buf);
}

static void brightness_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    int val = lv_slider_get_value(sl);
    backlight_set((uint8_t)val);
    prefs_set_brightness((uint8_t)val);
    if (lbl_bl_val) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val * 100 / 255);
        lv_label_set_text(lbl_bl_val, buf);
    }
}

static void invert_cb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool inverted = lv_obj_has_state(sw, LV_STATE_CHECKED);
    display_set_inverted(inverted);
    prefs_set_inverted(inverted);
}

lv_obj_t* screen_settings_create() {
    lv_obj_t* scr = ui_create_screen();

    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Scrollable container for all settings content
    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 310, 200);
    lv_obj_set_pos(cont, 5, 35);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);

    // ── Brightness slider ──
    lv_obj_t* bl_row = lv_obj_create(cont);
    lv_obj_remove_style_all(bl_row);
    lv_obj_set_size(bl_row, 300, 30);
    lv_obj_clear_flag(bl_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bl_label = lv_label_create(bl_row);
    lv_label_set_text(bl_label, "Brightness");
    lv_obj_set_style_text_color(bl_label, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(bl_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(bl_label, 0, 8);

    uint8_t saved_bright = prefs_get_brightness();

    slider_bl = lv_slider_create(bl_row);
    lv_obj_set_size(slider_bl, 150, 10);
    lv_obj_set_pos(slider_bl, 85, 10);
    lv_slider_set_range(slider_bl, 20, 255);
    lv_slider_set_value(slider_bl, saved_bright, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_bl, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_color(slider_bl, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_bl, UI_COLOR_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_bl, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_bl_val = lv_label_create(bl_row);
    {
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", saved_bright * 100 / 255);
        lv_label_set_text(lbl_bl_val, pct);
    }
    lv_obj_set_style_text_color(lbl_bl_val, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_bl_val, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_bl_val, 250, 8);

    // ── Invert display toggle ──
    lv_obj_t* inv_row = lv_obj_create(cont);
    lv_obj_remove_style_all(inv_row);
    lv_obj_set_size(inv_row, 300, 30);
    lv_obj_clear_flag(inv_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* inv_label = lv_label_create(inv_row);
    lv_label_set_text(inv_label, "Invert Colors");
    lv_obj_set_style_text_color(inv_label, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(inv_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(inv_label, 0, 6);

    sw_invert = lv_switch_create(inv_row);
    lv_obj_set_pos(sw_invert, 110, 2);
    lv_obj_set_size(sw_invert, 40, 22);
    if (prefs_get_inverted()) lv_obj_add_state(sw_invert, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw_invert, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_color(sw_invert, UI_COLOR_SUCCESS, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_invert, invert_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Info text ──
    lbl_info = lv_label_create(cont);
    lv_obj_set_style_text_color(lbl_info, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_12, 0);
    lv_label_set_text(lbl_info, "Loading...");

    update_info_text();
    return scr;
}

void screen_settings_update() {
    static uint32_t last_update = 0;
    if (millis() - last_update > 2000) {
        last_update = millis();
        update_info_text();
    }
}
