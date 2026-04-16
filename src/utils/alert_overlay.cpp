#include "alert_overlay.h"
#include "alert_state.h"
#include "../hal/sound.h"
#include "../ui/ui_common.h"
#include <lvgl.h>

static lv_obj_t* overlay_ = nullptr;

static void cleanup_overlay() {
    if (overlay_) {
        lv_obj_del(overlay_);
        overlay_ = nullptr;
    }
}

bool alert_overlay_active() { return overlay_ != nullptr; }

void alert_overlay_show_timer() {
    if (overlay_) return;
    sound_alarm_start();  // repeating sound until dismissed

    lv_obj_t* scr = lv_scr_act();
    overlay_ = lv_obj_create(scr);
    lv_obj_set_size(overlay_, 260, 120);
    lv_obj_center(overlay_);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(overlay_, 16, 0);
    lv_obj_set_style_border_color(overlay_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_border_width(overlay_, 3, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(overlay_);
    lv_label_set_text(lbl, LV_SYMBOL_BELL " Timer Done!");
    lv_obj_set_style_text_color(lbl, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t* btn = ui_create_btn(overlay_, "Dismiss", 110, 34);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        sound_alarm_stop();
        cleanup_overlay();
    }, LV_EVENT_CLICKED, NULL);

    // Auto-cleanup if screen is destroyed
    lv_obj_add_event_cb(overlay_, [](lv_event_t*) {
        overlay_ = nullptr;
    }, LV_EVENT_DELETE, NULL);
}

void alert_overlay_show_alarm() {
    if (overlay_) return;
    sound_alarm_start();

    lv_obj_t* scr = lv_scr_act();
    overlay_ = lv_obj_create(scr);
    lv_obj_set_size(overlay_, 260, 130);
    lv_obj_center(overlay_);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(overlay_, 16, 0);
    lv_obj_set_style_border_color(overlay_, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(overlay_, 3, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(overlay_);
    lv_label_set_text(lbl, LV_SYMBOL_BELL " Alarm!");
    lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t* snooze = ui_create_btn(overlay_, "Snooze 5m", 110, 34);
    lv_obj_align(snooze, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    lv_obj_add_event_cb(snooze, [](lv_event_t*) {
        sound_alarm_stop();
        alert_state_snooze_alarm();
        cleanup_overlay();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* stop = ui_create_btn(overlay_, "Stop", 90, 34);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_add_event_cb(stop, [](lv_event_t*) {
        sound_alarm_stop();
        alert_state_cancel_alarm();
        cleanup_overlay();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(overlay_, [](lv_event_t*) {
        overlay_ = nullptr;
    }, LV_EVENT_DELETE, NULL);
}
