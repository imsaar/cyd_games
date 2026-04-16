#include "clock_app.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../utils/alert_state.h"
#include "../../net/ntp_time.h"
#include <Arduino.h>
#include <time.h>

static lv_obj_t* screen_ = nullptr;
static lv_obj_t* tabview_ = nullptr;

// ── Clock tab ──
static lv_obj_t* lbl_clock_time_ = nullptr;
static lv_obj_t* lbl_clock_date_ = nullptr;

// ── Timer tab ──
static lv_obj_t* lbl_timer_mm_ = nullptr;
static lv_obj_t* lbl_timer_ss_ = nullptr;
static lv_obj_t* lbl_timer_remain_ = nullptr;
static lv_obj_t* btn_timer_start_ = nullptr;
static int timer_set_min_ = 5;
static int timer_set_sec_ = 0;

// ── Stopwatch tab ──
static lv_obj_t* lbl_sw_time_ = nullptr;
static lv_obj_t* lbl_sw_laps_ = nullptr;
static lv_obj_t* btn_sw_start_ = nullptr;
static lv_obj_t* btn_sw_lap_ = nullptr;
static bool sw_running_ = false;
static uint32_t sw_start_ms_ = 0;
static uint32_t sw_accum_ms_ = 0;
static uint32_t sw_laps_[20] = {};
static int sw_lap_count_ = 0;

// ── Alarm tab ──
static lv_obj_t* lbl_alarm_hh_ = nullptr;
static lv_obj_t* lbl_alarm_mm_ = nullptr;
static lv_obj_t* lbl_alarm_ampm_ = nullptr;
static lv_obj_t* lbl_alarm_status_ = nullptr;
static lv_obj_t* btn_alarm_set_ = nullptr;
static int alarm_set_hr12_ = 7;
static bool alarm_set_pm_ = false;

static void back_cb(lv_event_t*) { screen_manager_back_to_menu(); }

// ── Helper: format elapsed ms ──
static void fmt_elapsed(uint32_t ms, char* buf, int sz, bool centis) {
    int total_s = ms / 1000;
    int m = total_s / 60, s = total_s % 60;
    if (centis) {
        int cs = (ms % 1000) / 10;
        snprintf(buf, sz, "%02d:%02d.%02d", m, s, cs);
    } else {
        snprintf(buf, sz, "%02d:%02d", m, s);
    }
}

// ══════════════════════════════════════════
// ── Clock tab ──
// ══════════════════════════════════════════

static void build_clock_tab(lv_obj_t* tab) {
    lbl_clock_time_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_time_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_clock_time_, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_clock_time_, LV_ALIGN_CENTER, 0, -16);
    lv_label_set_text(lbl_clock_time_, "--:--:--");

    lbl_clock_date_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_date_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_clock_date_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_clock_date_, LV_ALIGN_CENTER, 0, 16);
    lv_label_set_text(lbl_clock_date_, "");
}

static void update_clock_tab() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    int hr = t.tm_hour % 12; if (hr == 0) hr = 12;
    const char* ap = t.tm_hour >= 12 ? "PM" : "AM";
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d %s", hr, t.tm_min, t.tm_sec, ap);
    if (lbl_clock_time_) lv_label_set_text(lbl_clock_time_, tbuf);

    static const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};
    char dbuf[24];
    snprintf(dbuf, sizeof(dbuf), "%s, %s %d %d", days[t.tm_wday],
             months[t.tm_mon], t.tm_mday, 1900 + t.tm_year);
    if (lbl_clock_date_) lv_label_set_text(lbl_clock_date_, dbuf);
}

// ══════════════════════════════════════════
// ── Timer tab ──
// ══════════════════════════════════════════

static void refresh_timer_display() {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", timer_set_min_);
    if (lbl_timer_mm_) lv_label_set_text(lbl_timer_mm_, buf);
    snprintf(buf, sizeof(buf), "%02d", timer_set_sec_);
    if (lbl_timer_ss_) lv_label_set_text(lbl_timer_ss_, buf);
}

static void build_timer_tab(lv_obj_t* tab) {
    int cx = 150;  // center of tab content
    int y = 10;

    // MM label
    lbl_timer_mm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_timer_mm_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_timer_mm_, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_timer_mm_, cx - 60, y);

    lv_obj_t* colon = lv_label_create(tab);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(colon, UI_COLOR_DIM, 0);
    lv_obj_set_pos(colon, cx - 8, y);

    // SS label
    lbl_timer_ss_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_timer_ss_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_timer_ss_, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_timer_ss_, cx + 12, y);

    y += 34;

    // +/- buttons for MM
    lv_obj_t* mm_up = ui_create_btn(tab, "+", 36, 26);
    lv_obj_set_pos(mm_up, cx - 66, y);
    lv_obj_add_event_cb(mm_up, [](lv_event_t*) {
        if (timer_set_min_ < 99) timer_set_min_++;
        refresh_timer_display();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* mm_dn = ui_create_btn(tab, "-", 36, 26);
    lv_obj_set_pos(mm_dn, cx - 26, y);
    lv_obj_add_event_cb(mm_dn, [](lv_event_t*) {
        if (timer_set_min_ > 0) timer_set_min_--;
        refresh_timer_display();
    }, LV_EVENT_CLICKED, NULL);

    // +/- buttons for SS
    lv_obj_t* ss_up = ui_create_btn(tab, "+", 36, 26);
    lv_obj_set_pos(ss_up, cx + 6, y);
    lv_obj_add_event_cb(ss_up, [](lv_event_t*) {
        if (timer_set_sec_ < 59) timer_set_sec_++;
        refresh_timer_display();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ss_dn = ui_create_btn(tab, "-", 36, 26);
    lv_obj_set_pos(ss_dn, cx + 46, y);
    lv_obj_add_event_cb(ss_dn, [](lv_event_t*) {
        if (timer_set_sec_ > 0) timer_set_sec_--;
        refresh_timer_display();
    }, LV_EVENT_CLICKED, NULL);

    y += 32;

    // Start/Stop button
    btn_timer_start_ = ui_create_btn(tab, "Start", 100, 30);
    lv_obj_set_pos(btn_timer_start_, cx - 50, y);
    lv_obj_add_event_cb(btn_timer_start_, [](lv_event_t*) {
        if (alert_state_timer_running()) {
            alert_state_cancel_timer();
            if (btn_timer_start_) lv_label_set_text(lv_obj_get_child(btn_timer_start_, 0), "Start");
            if (lbl_timer_remain_) lv_label_set_text(lbl_timer_remain_, "Cancelled");
        } else {
            uint32_t secs = timer_set_min_ * 60 + timer_set_sec_;
            if (secs == 0) return;
            alert_state_set_timer(secs);
            if (btn_timer_start_) lv_label_set_text(lv_obj_get_child(btn_timer_start_, 0), "Stop");
        }
    }, LV_EVENT_CLICKED, NULL);

    y += 36;

    // Remaining display
    lbl_timer_remain_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_timer_remain_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_timer_remain_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_timer_remain_, cx - 50, y);
    lv_label_set_text(lbl_timer_remain_, "");

    refresh_timer_display();
}

static void update_timer_tab() {
    if (alert_state_timer_running()) {
        uint32_t rem = alert_state_timer_remaining_ms();
        char buf[10];
        fmt_elapsed(rem, buf, sizeof(buf), false);
        if (lbl_timer_remain_) lv_label_set_text(lbl_timer_remain_, buf);
    }
    // Check if timer just finished while we're on this screen
    if (!alert_state_timer_running() && btn_timer_start_) {
        lv_label_set_text(lv_obj_get_child(btn_timer_start_, 0), "Start");
    }
}

// ══════════════════════════════════════════
// ── Stopwatch tab ──
// ══════════════════════════════════════════

static uint32_t sw_elapsed() {
    if (sw_running_) return sw_accum_ms_ + (millis() - sw_start_ms_);
    return sw_accum_ms_;
}

static void build_stopwatch_tab(lv_obj_t* tab) {
    int cx = 150;

    lbl_sw_time_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_sw_time_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_sw_time_, UI_COLOR_TEXT, 0);
    lv_obj_align(lbl_sw_time_, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(lbl_sw_time_, "00:00.00");

    btn_sw_start_ = ui_create_btn(tab, "Start", 80, 30);
    lv_obj_set_pos(btn_sw_start_, cx - 90, 50);
    lv_obj_add_event_cb(btn_sw_start_, [](lv_event_t*) {
        if (sw_running_) {
            sw_accum_ms_ += millis() - sw_start_ms_;
            sw_running_ = false;
            if (btn_sw_start_) lv_label_set_text(lv_obj_get_child(btn_sw_start_, 0), "Start");
            if (btn_sw_lap_) lv_label_set_text(lv_obj_get_child(btn_sw_lap_, 0), "Reset");
        } else {
            sw_start_ms_ = millis();
            sw_running_ = true;
            if (btn_sw_start_) lv_label_set_text(lv_obj_get_child(btn_sw_start_, 0), "Stop");
            if (btn_sw_lap_) lv_label_set_text(lv_obj_get_child(btn_sw_lap_, 0), "Lap");
        }
    }, LV_EVENT_CLICKED, NULL);

    btn_sw_lap_ = ui_create_btn(tab, "Reset", 80, 30);
    lv_obj_set_pos(btn_sw_lap_, cx + 10, 50);
    lv_obj_add_event_cb(btn_sw_lap_, [](lv_event_t*) {
        if (sw_running_) {
            // Lap
            if (sw_lap_count_ < 20) {
                sw_laps_[sw_lap_count_++] = sw_elapsed();
            }
            // Update lap display
            if (lbl_sw_laps_) {
                static char lap_buf[200];
                lap_buf[0] = '\0';
                for (int i = sw_lap_count_ - 1; i >= 0 && i >= sw_lap_count_ - 5; i--) {
                    char line[24];
                    char t[12];
                    fmt_elapsed(sw_laps_[i], t, sizeof(t), true);
                    snprintf(line, sizeof(line), "Lap %d: %s\n", i + 1, t);
                    strncat(lap_buf, line, sizeof(lap_buf) - strlen(lap_buf) - 1);
                }
                lv_label_set_text(lbl_sw_laps_, lap_buf);
            }
        } else {
            // Reset
            sw_accum_ms_ = 0;
            sw_lap_count_ = 0;
            if (lbl_sw_time_) lv_label_set_text(lbl_sw_time_, "00:00.00");
            if (lbl_sw_laps_) lv_label_set_text(lbl_sw_laps_, "");
        }
    }, LV_EVENT_CLICKED, NULL);

    lbl_sw_laps_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_sw_laps_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_sw_laps_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_sw_laps_, 20, 86);
    lv_label_set_text(lbl_sw_laps_, "");
}

static void update_stopwatch_tab() {
    if (sw_running_ && lbl_sw_time_) {
        char buf[12];
        fmt_elapsed(sw_elapsed(), buf, sizeof(buf), true);
        lv_label_set_text(lbl_sw_time_, buf);
    }
}

// ══════════════════════════════════════════
// ── Alarm tab ──
// ══════════════════════════════════════════

static void refresh_alarm_display() {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", alarm_set_hr12_);
    if (lbl_alarm_hh_) lv_label_set_text(lbl_alarm_hh_, buf);
    snprintf(buf, sizeof(buf), "%02d", (int)alert_state_alarm_minute());
    if (lbl_alarm_mm_) lv_label_set_text(lbl_alarm_mm_, buf);
    if (lbl_alarm_ampm_) lv_label_set_text(lbl_alarm_ampm_, alarm_set_pm_ ? "PM" : "AM");
}

static void refresh_alarm_status() {
    if (!lbl_alarm_status_) return;
    if (alert_state_alarm_enabled()) {
        int hr = alert_state_alarm_hour() % 12;
        if (hr == 0) hr = 12;
        const char* ap = alert_state_alarm_hour() >= 12 ? "PM" : "AM";
        char buf[24];
        snprintf(buf, sizeof(buf), "Alarm: %d:%02d %s ON", hr, alert_state_alarm_minute(), ap);
        lv_label_set_text(lbl_alarm_status_, buf);
        lv_obj_set_style_text_color(lbl_alarm_status_, UI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(lbl_alarm_status_, "Alarm: OFF");
        lv_obj_set_style_text_color(lbl_alarm_status_, UI_COLOR_DIM, 0);
    }
    if (btn_alarm_set_) {
        lv_label_set_text(lv_obj_get_child(btn_alarm_set_, 0),
                          alert_state_alarm_enabled() ? "Cancel" : "Set Alarm");
    }
}

static void build_alarm_tab(lv_obj_t* tab) {
    int cx = 150;
    int y = 10;

    // HH label
    lbl_alarm_hh_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_hh_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_alarm_hh_, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_alarm_hh_, cx - 70, y);

    lv_obj_t* colon = lv_label_create(tab);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(colon, UI_COLOR_DIM, 0);
    lv_obj_set_pos(colon, cx - 18, y);

    // MM label
    lbl_alarm_mm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_mm_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_alarm_mm_, UI_COLOR_TEXT, 0);
    lv_obj_set_pos(lbl_alarm_mm_, cx + 2, y);

    // AM/PM toggle
    lbl_alarm_ampm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_ampm_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_alarm_ampm_, UI_COLOR_WARNING, 0);
    lv_obj_set_pos(lbl_alarm_ampm_, cx + 52, y + 6);

    y += 34;

    // +/- for HH
    lv_obj_t* hh_up = ui_create_btn(tab, "+", 36, 26);
    lv_obj_set_pos(hh_up, cx - 76, y);
    lv_obj_add_event_cb(hh_up, [](lv_event_t*) {
        alarm_set_hr12_ = (alarm_set_hr12_ % 12) + 1;
        refresh_alarm_display();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* hh_dn = ui_create_btn(tab, "-", 36, 26);
    lv_obj_set_pos(hh_dn, cx - 36, y);
    lv_obj_add_event_cb(hh_dn, [](lv_event_t*) {
        alarm_set_hr12_ = (alarm_set_hr12_ == 1) ? 12 : alarm_set_hr12_ - 1;
        refresh_alarm_display();
    }, LV_EVENT_CLICKED, NULL);

    // +/- for MM
    lv_obj_t* mm_up = ui_create_btn(tab, "+", 36, 26);
    lv_obj_set_pos(mm_up, cx - 4, y);
    lv_obj_add_event_cb(mm_up, [](lv_event_t*) {
        uint8_t m = (alert_state_alarm_minute() + 1) % 60;
        alert_state_set_alarm(alert_state_alarm_hour(), m);
        refresh_alarm_display();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* mm_dn = ui_create_btn(tab, "-", 36, 26);
    lv_obj_set_pos(mm_dn, cx + 36, y);
    lv_obj_add_event_cb(mm_dn, [](lv_event_t*) {
        uint8_t m = (alert_state_alarm_minute() == 0) ? 59 : alert_state_alarm_minute() - 1;
        alert_state_set_alarm(alert_state_alarm_hour(), m);
        refresh_alarm_display();
    }, LV_EVENT_CLICKED, NULL);

    // AM/PM toggle button
    lv_obj_t* ampm_btn = ui_create_btn(tab, "AM/PM", 56, 26);
    lv_obj_set_pos(ampm_btn, cx + 76, y);
    lv_obj_add_event_cb(ampm_btn, [](lv_event_t*) {
        alarm_set_pm_ = !alarm_set_pm_;
        refresh_alarm_display();
    }, LV_EVENT_CLICKED, NULL);

    y += 34;

    // Set / Cancel button
    btn_alarm_set_ = ui_create_btn(tab, "Set Alarm", 120, 30);
    lv_obj_set_pos(btn_alarm_set_, cx - 60, y);
    lv_obj_add_event_cb(btn_alarm_set_, [](lv_event_t*) {
        if (alert_state_alarm_enabled()) {
            alert_state_cancel_alarm();
        } else {
            int hr24 = alarm_set_hr12_ % 12;
            if (alarm_set_pm_) hr24 += 12;
            alert_state_set_alarm(hr24, alert_state_alarm_minute());
            alert_state_enable_alarm(true);
        }
        refresh_alarm_status();
    }, LV_EVENT_CLICKED, NULL);

    y += 36;

    // Status
    lbl_alarm_status_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_status_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_alarm_status_, cx - 70, y);

    refresh_alarm_display();
    refresh_alarm_status();
}

// ══════════════════════════════════════════
// ── Lifecycle ──
// ══════════════════════════════════════════

lv_obj_t* clock_app_create() {
    screen_ = ui_create_screen();
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Back button top-left
    {
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, 30, 22);
        lv_obj_set_pos(btn, 2, 2);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    }

    // Tabview
    tabview_ = lv_tabview_create(screen_, LV_DIR_TOP, 28);
    lv_obj_set_size(tabview_, 320, 214);
    lv_obj_set_pos(tabview_, 0, 26);
    lv_obj_set_style_bg_color(tabview_, UI_COLOR_BG, 0);

    // Style tab buttons
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview_);
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(tab_btns, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_PRIMARY, LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t* t1 = lv_tabview_add_tab(tabview_, "Clock");
    lv_obj_t* t2 = lv_tabview_add_tab(tabview_, "Timer");
    lv_obj_t* t3 = lv_tabview_add_tab(tabview_, "Watch");
    lv_obj_t* t4 = lv_tabview_add_tab(tabview_, "Alarm");

    // Disable scrolling on all tabs
    lv_obj_t* tabs[] = {t1, t2, t3, t4};
    for (auto* t : tabs) {
        lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(t, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
    }

    build_clock_tab(t1);
    build_timer_tab(t2);
    build_stopwatch_tab(t3);
    build_alarm_tab(t4);

    return screen_;
}

void clock_app_update() {
    static uint32_t last_update = 0;
    if (millis() - last_update < 100) return;  // 10 Hz
    last_update = millis();

    uint16_t active = lv_tabview_get_tab_act(tabview_);
    switch (active) {
        case 0: update_clock_tab(); break;
        case 1: update_timer_tab(); break;
        case 2: update_stopwatch_tab(); break;
        default: break;
    }
}

void clock_app_destroy() {
    screen_ = nullptr;
    tabview_ = nullptr;
    lbl_clock_time_ = nullptr;
    lbl_clock_date_ = nullptr;
    lbl_timer_mm_ = nullptr;
    lbl_timer_ss_ = nullptr;
    lbl_timer_remain_ = nullptr;
    btn_timer_start_ = nullptr;
    lbl_sw_time_ = nullptr;
    lbl_sw_laps_ = nullptr;
    btn_sw_start_ = nullptr;
    btn_sw_lap_ = nullptr;
    lbl_alarm_hh_ = nullptr;
    lbl_alarm_mm_ = nullptr;
    lbl_alarm_ampm_ = nullptr;
    lbl_alarm_status_ = nullptr;
    btn_alarm_set_ = nullptr;
}
