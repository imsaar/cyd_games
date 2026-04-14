#include "screen_settings.h"
#include "ui_common.h"
#include "screen_manager.h"
#include "../hal/backlight.h"
#include "../hal/prefs.h"
#include "../hal/display.h"
#include "../net/wifi_manager.h"
#include "../net/discovery.h"
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

static lv_obj_t* lbl_name_val = nullptr;
static lv_obj_t* name_overlay = nullptr;

static void close_name_editor(lv_event_t* e);
static void name_kb_ready(lv_event_t* e);

static void open_name_editor(lv_event_t* e) {
    if (name_overlay) return;

    lv_obj_t* scr = lv_scr_act();
    name_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(name_overlay);
    lv_obj_set_size(name_overlay, 320, 240);
    lv_obj_set_pos(name_overlay, 0, 0);
    lv_obj_set_style_bg_color(name_overlay, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(name_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(name_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(name_overlay);
    lv_label_set_text(title, "Device Name");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t* ta = lv_textarea_create(name_overlay);
    lv_obj_set_size(ta, 200, 36);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 25);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 11);
    lv_textarea_set_text(ta, discovery_get_name());
    lv_obj_set_style_bg_color(ta, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(ta, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_16, 0);
    lv_obj_set_style_border_color(ta, UI_COLOR_PRIMARY, 0);

    lv_obj_t* kb = lv_keyboard_create(name_overlay);
    lv_obj_set_size(kb, 320, 170);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    // Save on OK, close on Cancel
    lv_obj_add_event_cb(kb, name_kb_ready, LV_EVENT_READY, ta);
    lv_obj_add_event_cb(kb, close_name_editor, LV_EVENT_CANCEL, NULL);
}

static void name_kb_ready(lv_event_t* e) {
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_user_data(e);
    const char* text = lv_textarea_get_text(ta);
    if (text && text[0] != '\0') {
        discovery_set_name(text);
        if (lbl_name_val) {
            lv_label_set_text(lbl_name_val, text);
        }
    }
    if (name_overlay) {
        lv_obj_del(name_overlay);
        name_overlay = nullptr;
    }
}

static void close_name_editor(lv_event_t* e) {
    if (name_overlay) {
        lv_obj_del(name_overlay);
        name_overlay = nullptr;
    }
}

static lv_obj_t* lbl_wifi_status = nullptr;

static void update_wifi_status() {
    if (!lbl_wifi_status) return;
    if (wifi_disabled()) {
        lv_label_set_text(lbl_wifi_status, "OFF (ESP-NOW)");
        lv_obj_set_style_text_color(lbl_wifi_status, UI_COLOR_WARNING, 0);
    } else if (wifi_connected()) {
        lv_label_set_text(lbl_wifi_status, "Connected");
        lv_obj_set_style_text_color(lbl_wifi_status, UI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(lbl_wifi_status, "Connecting...");
        lv_obj_set_style_text_color(lbl_wifi_status, UI_COLOR_DIM, 0);
    }
}

static void wifi_toggle_cb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (!enabled) {
        wifi_disable();
        discovery_reinit();  // Switch to ESP-NOW
    } else {
        wifi_enable();
        // Discovery will reinit on next settings open or can be triggered
        // For now, reinit immediately (will start in UDP if WiFi reconnects fast)
        discovery_reinit();
    }
    update_wifi_status();
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

    // ── Device name ──
    lv_obj_t* name_row = lv_obj_create(cont);
    lv_obj_remove_style_all(name_row);
    lv_obj_set_size(name_row, 300, 30);
    lv_obj_clear_flag(name_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_label = lv_label_create(name_row);
    lv_label_set_text(name_label, "Name");
    lv_obj_set_style_text_color(name_label, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(name_label, 0, 6);

    lbl_name_val = lv_label_create(name_row);
    lv_label_set_text(lbl_name_val, discovery_get_name());
    lv_obj_set_style_text_color(lbl_name_val, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_name_val, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_name_val, 55, 6);

    lv_obj_t* btn_edit = ui_create_btn(name_row, LV_SYMBOL_EDIT, 36, 24);
    lv_obj_set_pos(btn_edit, 180, 2);
    lv_obj_add_event_cb(btn_edit, open_name_editor, LV_EVENT_CLICKED, NULL);

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

    // ── WiFi toggle ──
    lv_obj_t* wifi_row = lv_obj_create(cont);
    lv_obj_remove_style_all(wifi_row);
    lv_obj_set_size(wifi_row, 300, 30);
    lv_obj_clear_flag(wifi_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* wifi_label = lv_label_create(wifi_row);
    lv_label_set_text(wifi_label, "WiFi");
    lv_obj_set_style_text_color(wifi_label, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(wifi_label, 0, 6);

    lv_obj_t* sw_wifi = lv_switch_create(wifi_row);
    lv_obj_set_pos(sw_wifi, 110, 2);
    lv_obj_set_size(sw_wifi, 40, 22);
    if (!wifi_disabled()) lv_obj_add_state(sw_wifi, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw_wifi, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_color(sw_wifi, UI_COLOR_SUCCESS, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_wifi, wifi_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_wifi_status = lv_label_create(wifi_row);
    lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_wifi_status, 165, 6);
    update_wifi_status();

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
