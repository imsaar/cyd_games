#include "screen_wifi.h"
#include "ui_common.h"
#include "screen_manager.h"
#include "../net/wifi_manager.h"
#include "../net/discovery.h"
#include <Arduino.h>
#include <string.h>

static char ssid_buf[64] = "";
static char pass_buf[64] = "";

static lv_obj_t* lbl_ssid_val = nullptr;
static lv_obj_t* lbl_pass_val = nullptr;
static lv_obj_t* lbl_status   = nullptr;
static lv_obj_t* btn_test     = nullptr;
static lv_obj_t* btn_save     = nullptr;
static lv_obj_t* kb_overlay   = nullptr;
static lv_obj_t* kb_textarea  = nullptr;

static enum { EDIT_NONE, EDIT_SSID, EDIT_PASS } edit_field = EDIT_NONE;

static void render_ssid() {
    if (!lbl_ssid_val) return;
    if (ssid_buf[0] == '\0') {
        lv_label_set_text(lbl_ssid_val, "(not set)");
        lv_obj_set_style_text_color(lbl_ssid_val, UI_COLOR_DIM, 0);
    } else {
        lv_label_set_text(lbl_ssid_val, ssid_buf);
        lv_obj_set_style_text_color(lbl_ssid_val, UI_COLOR_TEXT, 0);
    }
}

static void render_pass() {
    if (!lbl_pass_val) return;
    if (pass_buf[0] == '\0') {
        lv_label_set_text(lbl_pass_val, "(none)");
        lv_obj_set_style_text_color(lbl_pass_val, UI_COLOR_DIM, 0);
    } else {
        char masked[32];
        int n = (int)strlen(pass_buf);
        if (n > 16) n = 16;
        for (int i = 0; i < n; i++) masked[i] = '*';
        masked[n] = '\0';
        lv_label_set_text(lbl_pass_val, masked);
        lv_obj_set_style_text_color(lbl_pass_val, UI_COLOR_TEXT, 0);
    }
}

static void render_status() {
    if (!lbl_status) return;
    switch (wifi_test_state()) {
        case WIFI_TEST_CONNECTING:
            lv_label_set_text(lbl_status, LV_SYMBOL_REFRESH " Connecting...");
            lv_obj_set_style_text_color(lbl_status, UI_COLOR_WARNING, 0);
            break;
        case WIFI_TEST_SUCCESS:
            lv_label_set_text(lbl_status, LV_SYMBOL_OK " Connected");
            lv_obj_set_style_text_color(lbl_status, UI_COLOR_SUCCESS, 0);
            break;
        case WIFI_TEST_FAILED:
            lv_label_set_text(lbl_status, LV_SYMBOL_CLOSE " Failed");
            lv_obj_set_style_text_color(lbl_status, UI_COLOR_ACCENT, 0);
            break;
        default:
            if (wifi_connected()) {
                lv_label_set_text(lbl_status, LV_SYMBOL_WIFI " Connected");
                lv_obj_set_style_text_color(lbl_status, UI_COLOR_SUCCESS, 0);
            } else if (wifi_has_credentials()) {
                lv_label_set_text(lbl_status, "Not connected");
                lv_obj_set_style_text_color(lbl_status, UI_COLOR_DIM, 0);
            } else {
                lv_label_set_text(lbl_status, "Enter SSID & password");
                lv_obj_set_style_text_color(lbl_status, UI_COLOR_DIM, 0);
            }
            break;
    }
}

static void close_kb_overlay() {
    if (kb_overlay) {
        lv_obj_del(kb_overlay);
        kb_overlay  = nullptr;
        kb_textarea = nullptr;
    }
    edit_field = EDIT_NONE;
}

static void kb_ready_cb(lv_event_t* e) {
    if (!kb_textarea) return;
    const char* text = lv_textarea_get_text(kb_textarea);
    if (edit_field == EDIT_SSID) {
        strncpy(ssid_buf, text ? text : "", sizeof(ssid_buf) - 1);
        ssid_buf[sizeof(ssid_buf) - 1] = '\0';
        render_ssid();
    } else if (edit_field == EDIT_PASS) {
        strncpy(pass_buf, text ? text : "", sizeof(pass_buf) - 1);
        pass_buf[sizeof(pass_buf) - 1] = '\0';
        render_pass();
    }
    close_kb_overlay();
}

static void kb_cancel_cb(lv_event_t* e) {
    close_kb_overlay();
}

static void open_editor(const char* title_text, const char* current, bool is_password) {
    if (kb_overlay) return;

    lv_obj_t* scr = lv_scr_act();
    kb_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(kb_overlay);
    lv_obj_set_size(kb_overlay, 320, 240);
    lv_obj_set_pos(kb_overlay, 0, 0);
    lv_obj_set_style_bg_color(kb_overlay, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(kb_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(kb_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(kb_overlay);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    kb_textarea = lv_textarea_create(kb_overlay);
    lv_obj_set_size(kb_textarea, 280, 36);
    lv_obj_align(kb_textarea, LV_ALIGN_TOP_MID, 0, 25);
    lv_textarea_set_one_line(kb_textarea, true);
    lv_textarea_set_max_length(kb_textarea, 63);
    lv_textarea_set_password_mode(kb_textarea, is_password);
    lv_textarea_set_text(kb_textarea, current ? current : "");
    lv_obj_set_style_bg_color(kb_textarea, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(kb_textarea, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(kb_textarea, &lv_font_montserrat_16, 0);
    lv_obj_set_style_border_color(kb_textarea, UI_COLOR_PRIMARY, 0);

    lv_obj_t* kb = lv_keyboard_create(kb_overlay);
    lv_obj_set_size(kb, 320, 170);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, kb_textarea);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    lv_obj_add_event_cb(kb, kb_ready_cb,  LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(kb, kb_cancel_cb, LV_EVENT_CANCEL, NULL);
}

static void edit_ssid_cb(lv_event_t* e) {
    edit_field = EDIT_SSID;
    open_editor("SSID", ssid_buf, false);
}

static void edit_pass_cb(lv_event_t* e) {
    edit_field = EDIT_PASS;
    open_editor("Password", pass_buf, true);
}

static void test_cb(lv_event_t* e) {
    if (ssid_buf[0] == '\0') return;
    wifi_test_start(ssid_buf, pass_buf);
    render_status();
}

static void save_cb(lv_event_t* e) {
    if (ssid_buf[0] == '\0') return;
    wifi_save_credentials(ssid_buf, pass_buf);
    discovery_reinit();
    screen_manager_back_to_menu();
}

lv_obj_t* screen_wifi_create() {
    // Pull current credentials so the user starts from what's saved
    wifi_get_ssid(ssid_buf, sizeof(ssid_buf));
    pass_buf[0] = '\0';  // Don't expose stored password; user re-enters if changing

    lv_obj_t* scr = ui_create_screen();

    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "WiFi Setup");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // SSID row
    lv_obj_t* ssid_label = lv_label_create(scr);
    lv_label_set_text(ssid_label, "SSID");
    lv_obj_set_style_text_color(ssid_label, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(ssid_label, 12, 50);

    lbl_ssid_val = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_ssid_val, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_ssid_val, 70, 50);
    lv_obj_set_width(lbl_ssid_val, 180);
    lv_label_set_long_mode(lbl_ssid_val, LV_LABEL_LONG_DOT);
    render_ssid();

    lv_obj_t* btn_ssid = ui_create_btn(scr, LV_SYMBOL_EDIT, 40, 28);
    lv_obj_set_pos(btn_ssid, 268, 45);
    lv_obj_add_event_cb(btn_ssid, edit_ssid_cb, LV_EVENT_CLICKED, NULL);

    // Password row
    lv_obj_t* pass_label = lv_label_create(scr);
    lv_label_set_text(pass_label, "Pass");
    lv_obj_set_style_text_color(pass_label, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(pass_label, 12, 90);

    lbl_pass_val = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_pass_val, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_pass_val, 70, 90);
    lv_obj_set_width(lbl_pass_val, 180);
    lv_label_set_long_mode(lbl_pass_val, LV_LABEL_LONG_DOT);
    render_pass();

    lv_obj_t* btn_pass = ui_create_btn(scr, LV_SYMBOL_EDIT, 40, 28);
    lv_obj_set_pos(btn_pass, 268, 85);
    lv_obj_add_event_cb(btn_pass, edit_pass_cb, LV_EVENT_CLICKED, NULL);

    // Status
    lbl_status = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 130);
    render_status();

    // Test / Save buttons
    btn_test = ui_create_btn(scr, "Test", 110, 36);
    lv_obj_set_pos(btn_test, 30, 188);
    lv_obj_add_event_cb(btn_test, test_cb, LV_EVENT_CLICKED, NULL);

    btn_save = ui_create_btn(scr, "Save", 110, 36);
    lv_obj_set_pos(btn_save, 180, 188);
    lv_obj_add_event_cb(btn_save, save_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

void screen_wifi_update() {
    static WifiTestState last_state = WIFI_TEST_IDLE;
    wifi_test_tick();
    WifiTestState s = wifi_test_state();
    if (s != last_state) {
        last_state = s;
        render_status();
    }
}

void screen_wifi_destroy() {
    if (wifi_test_state() != WIFI_TEST_IDLE) {
        wifi_test_cancel();
    }
    close_kb_overlay();
    lbl_ssid_val = nullptr;
    lbl_pass_val = nullptr;
    lbl_status   = nullptr;
    btn_test     = nullptr;
    btn_save     = nullptr;
}
