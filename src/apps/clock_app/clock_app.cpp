#include "clock_app.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../utils/alert_state.h"
#include "../../net/ntp_time.h"
#include <Arduino.h>
#include <time.h>
#include <math.h>

static lv_obj_t* screen_ = nullptr;
static lv_obj_t* tabview_ = nullptr;

// ── Clock tab ──
static lv_obj_t* lbl_clock_time_ = nullptr;
static lv_obj_t* lbl_clock_date_ = nullptr;
static lv_obj_t* lbl_clock_hijri_ = nullptr;
static lv_obj_t* arc_sec_ = nullptr;

// ── Timer tab ──
static lv_obj_t* lbl_timer_hh_ = nullptr;
static lv_obj_t* lbl_timer_mm_ = nullptr;
static lv_obj_t* lbl_timer_ss_ = nullptr;
static lv_obj_t* lbl_timer_countdown_ = nullptr;
static lv_obj_t* btn_timer_start_ = nullptr;
static lv_obj_t* timer_set_panel_ = nullptr;
static lv_obj_t* timer_run_panel_ = nullptr;
static int timer_set_hr_ = 0;
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

static void fmt_elapsed(uint32_t ms, char* buf, int sz, bool centis) {
    int total_s = ms / 1000;
    int h = total_s / 3600, m = (total_s % 3600) / 60, s = total_s % 60;
    if (centis) {
        int cs = (ms % 1000) / 10;
        if (h > 0) snprintf(buf, sz, "%d:%02d:%02d.%02d", h, m, s, cs);
        else snprintf(buf, sz, "%02d:%02d.%02d", m, s, cs);
    } else {
        if (h > 0) snprintf(buf, sz, "%d:%02d:%02d", h, m, s);
        else snprintf(buf, sz, "%02d:%02d", m, s);
    }
}

// ══════════════════════════════════════════
// ── Hijri date approximation ──
// ══════════════════════════════════════════

static void gregorian_to_hijri(int gy, int gm, int gd, int* hy, int* hm, int* hd) {
    // Julian Day Number
    int a = (14 - gm) / 12;
    int y = gy + 4800 - a;
    int m = gm + 12 * a - 3;
    long jd = gd + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
    // Hijri from JD (Kuwaiti algorithm)
    long l = jd - 1948440 + 10632;
    long n = (l - 1) / 10631;
    l = l - 10631 * n + 354;
    long j = ((long)(10985 - l) / 5316) * ((long)(50 * l) / 17719)
           + ((long)l / 5670) * ((long)(43 * l) / 15238);
    l = l - ((long)(30 - j) / 15) * ((long)(17719 * j) / 50)
      - ((long)j / 16) * ((long)(15238 * j) / 43) + 29;
    *hm = (int)(24 * l / 709);
    *hd = (int)(l - (long)(709 * (*hm)) / 24);
    *hy = (int)(30 * n + j - 30);
}

static const char* hijri_month_names[] = {
    "Muharram", "Safar", "Rabi I", "Rabi II",
    "Jumada I", "Jumada II", "Rajab", "Sha'ban",
    "Ramadan", "Shawwal", "Dhul Qi'dah", "Dhul Hijjah"
};

// ══════════════════════════════════════════
// ── Clock tab (fancy) ──
// ══════════════════════════════════════════

static void build_clock_tab(lv_obj_t* tab) {
    // Seconds arc ring
    arc_sec_ = lv_arc_create(tab);
    lv_obj_set_size(arc_sec_, 140, 140);
    lv_obj_align(arc_sec_, LV_ALIGN_LEFT_MID, 10, 0);
    lv_arc_set_rotation(arc_sec_, 270);
    lv_arc_set_range(arc_sec_, 0, 60);
    lv_arc_set_value(arc_sec_, 0);
    lv_arc_set_bg_angles(arc_sec_, 0, 360);
    lv_obj_set_style_arc_width(arc_sec_, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_sec_, lv_color_hex(0x1a2a4a), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_sec_, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_sec_, lv_color_hex(0x4ecca3), LV_PART_INDICATOR);
    lv_obj_remove_style(arc_sec_, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_sec_, LV_OBJ_FLAG_CLICKABLE);

    // Time inside arc
    lbl_clock_time_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_time_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_style_text_font(lbl_clock_time_, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_clock_time_, LV_ALIGN_LEFT_MID, 30, -10);
    lv_label_set_text(lbl_clock_time_, "--:--");

    // AM/PM below time in arc
    static lv_obj_t* lbl_ampm = nullptr;
    lbl_ampm = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_ampm, lv_color_hex(0xf0a500), 0);
    lv_obj_set_style_text_font(lbl_ampm, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_ampm, LV_ALIGN_LEFT_MID, 52, 14);
    lv_label_set_text(lbl_ampm, "");

    // Gregorian date (right side)
    lbl_clock_date_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_date_, lv_color_hex(0x88bbdd), 0);
    lv_obj_set_style_text_font(lbl_clock_date_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_clock_date_, 162, 30);
    lv_label_set_text(lbl_clock_date_, "");

    // Hijri date (right side, below)
    lbl_clock_hijri_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_hijri_, lv_color_hex(0xf0a500), 0);
    lv_obj_set_style_text_font(lbl_clock_hijri_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_clock_hijri_, 162, 52);
    lv_label_set_text(lbl_clock_hijri_, "");
}

static void update_clock_tab() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    int hr = t.tm_hour % 12; if (hr == 0) hr = 12;
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%d:%02d", hr, t.tm_min);
    if (lbl_clock_time_) lv_label_set_text(lbl_clock_time_, tbuf);

    // AM/PM label is the 3rd child of tab (index 2)
    lv_obj_t* tab = lv_obj_get_parent(lbl_clock_time_);
    lv_obj_t* ampm_lbl = lv_obj_get_child(tab, 2);
    if (ampm_lbl) lv_label_set_text(ampm_lbl, t.tm_hour >= 12 ? "PM" : "AM");

    // Seconds arc
    if (arc_sec_) lv_arc_set_value(arc_sec_, t.tm_sec);

    // Gregorian date
    static const char* days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char dbuf[32];
    snprintf(dbuf, sizeof(dbuf), "%s\n%s %d, %d", days[t.tm_wday], months[t.tm_mon], t.tm_mday, 1900 + t.tm_year);
    if (lbl_clock_date_) lv_label_set_text(lbl_clock_date_, dbuf);

    // Hijri date
    int hy, hm, hd;
    gregorian_to_hijri(1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, &hy, &hm, &hd);
    char hbuf[32];
    if (hm >= 1 && hm <= 12)
        snprintf(hbuf, sizeof(hbuf), "%d %s %d AH", hd, hijri_month_names[hm - 1], hy);
    else
        hbuf[0] = '\0';
    if (lbl_clock_hijri_) lv_label_set_text(lbl_clock_hijri_, hbuf);
}

// ══════════════════════════════════════════
// ── Timer tab ──
// ══════════════════════════════════════════

static void refresh_timer_set() {
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", timer_set_hr_);
    if (lbl_timer_hh_) lv_label_set_text(lbl_timer_hh_, buf);
    snprintf(buf, sizeof(buf), "%02d", timer_set_min_);
    if (lbl_timer_mm_) lv_label_set_text(lbl_timer_mm_, buf);
    snprintf(buf, sizeof(buf), "%02d", timer_set_sec_);
    if (lbl_timer_ss_) lv_label_set_text(lbl_timer_ss_, buf);
}

static void show_timer_set_mode() {
    if (timer_set_panel_) lv_obj_clear_flag(timer_set_panel_, LV_OBJ_FLAG_HIDDEN);
    if (timer_run_panel_) lv_obj_add_flag(timer_run_panel_, LV_OBJ_FLAG_HIDDEN);
}

static void show_timer_run_mode() {
    if (timer_set_panel_) lv_obj_add_flag(timer_set_panel_, LV_OBJ_FLAG_HIDDEN);
    if (timer_run_panel_) lv_obj_clear_flag(timer_run_panel_, LV_OBJ_FLAG_HIDDEN);
}

static void build_timer_tab(lv_obj_t* tab) {
    // ── Set panel (HH:MM:SS with +/- buttons) ──
    timer_set_panel_ = lv_obj_create(tab);
    lv_obj_remove_style_all(timer_set_panel_);
    lv_obj_set_size(timer_set_panel_, 300, 160);
    lv_obj_set_pos(timer_set_panel_, 0, 0);
    lv_obj_clear_flag(timer_set_panel_, LV_OBJ_FLAG_SCROLLABLE);

    int cx = 150, y = 8;

    // HH
    lbl_timer_hh_ = lv_label_create(timer_set_panel_);
    lv_obj_set_style_text_font(lbl_timer_hh_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_timer_hh_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_pos(lbl_timer_hh_, cx - 100, y);

    lv_obj_t* c1 = lv_label_create(timer_set_panel_);
    lv_label_set_text(c1, ":"); lv_obj_set_style_text_font(c1, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(c1, UI_COLOR_DIM, 0); lv_obj_set_pos(c1, cx - 48, y);

    // MM
    lbl_timer_mm_ = lv_label_create(timer_set_panel_);
    lv_obj_set_style_text_font(lbl_timer_mm_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_timer_mm_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_pos(lbl_timer_mm_, cx - 30, y);

    lv_obj_t* c2 = lv_label_create(timer_set_panel_);
    lv_label_set_text(c2, ":"); lv_obj_set_style_text_font(c2, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(c2, UI_COLOR_DIM, 0); lv_obj_set_pos(c2, cx + 22, y);

    // SS
    lbl_timer_ss_ = lv_label_create(timer_set_panel_);
    lv_obj_set_style_text_font(lbl_timer_ss_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_timer_ss_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_pos(lbl_timer_ss_, cx + 40, y);

    y += 44;

    // +/- for HH
    auto mk_btn = [&](const char* txt, int x, int by) {
        lv_obj_t* b = ui_create_btn(timer_set_panel_, txt, 32, 24);
        lv_obj_set_pos(b, x, by);
        return b;
    };
    lv_obj_t* hh_up = mk_btn("+", cx - 104, y);
    lv_obj_add_event_cb(hh_up, [](lv_event_t*) { if (timer_set_hr_ < 23) timer_set_hr_++; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* hh_dn = mk_btn("-", cx - 68, y);
    lv_obj_add_event_cb(hh_dn, [](lv_event_t*) { if (timer_set_hr_ > 0) timer_set_hr_--; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);

    // +/- for MM
    lv_obj_t* mm_up = mk_btn("+", cx - 34, y);
    lv_obj_add_event_cb(mm_up, [](lv_event_t*) { if (timer_set_min_ < 59) timer_set_min_++; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* mm_dn = mk_btn("-", cx + 2, y);
    lv_obj_add_event_cb(mm_dn, [](lv_event_t*) { if (timer_set_min_ > 0) timer_set_min_--; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);

    // +/- for SS
    lv_obj_t* ss_up = mk_btn("+", cx + 36, y);
    lv_obj_add_event_cb(ss_up, [](lv_event_t*) { if (timer_set_sec_ < 59) timer_set_sec_++; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ss_dn = mk_btn("-", cx + 72, y);
    lv_obj_add_event_cb(ss_dn, [](lv_event_t*) { if (timer_set_sec_ > 0) timer_set_sec_--; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);

    y += 32;

    // Start button
    lv_obj_t* start = ui_create_btn(timer_set_panel_, "Start", 110, 32);
    lv_obj_set_pos(start, cx - 55, y);
    lv_obj_add_event_cb(start, [](lv_event_t*) {
        uint32_t secs = timer_set_hr_ * 3600 + timer_set_min_ * 60 + timer_set_sec_;
        if (secs == 0) return;
        alert_state_set_timer(secs);
        show_timer_run_mode();
    }, LV_EVENT_CLICKED, NULL);

    refresh_timer_set();

    // ── Run panel (countdown display + cancel) ──
    timer_run_panel_ = lv_obj_create(tab);
    lv_obj_remove_style_all(timer_run_panel_);
    lv_obj_set_size(timer_run_panel_, 300, 160);
    lv_obj_set_pos(timer_run_panel_, 0, 0);
    lv_obj_clear_flag(timer_run_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(timer_run_panel_, LV_OBJ_FLAG_HIDDEN);

    lbl_timer_countdown_ = lv_label_create(timer_run_panel_);
    lv_obj_set_style_text_font(lbl_timer_countdown_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_timer_countdown_, lv_color_hex(0xe94560), 0);
    lv_obj_align(lbl_timer_countdown_, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(lbl_timer_countdown_, "00:00");

    lv_obj_t* cancel = ui_create_btn(timer_run_panel_, "Cancel", 100, 32);
    lv_obj_align(cancel, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) {
        alert_state_cancel_timer();
        show_timer_set_mode();
    }, LV_EVENT_CLICKED, NULL);
}

static void update_timer_tab() {
    if (alert_state_timer_running()) {
        uint32_t rem = alert_state_timer_remaining_ms();
        char buf[12];
        fmt_elapsed(rem, buf, sizeof(buf), false);
        if (lbl_timer_countdown_) lv_label_set_text(lbl_timer_countdown_, buf);
    } else if (timer_run_panel_ && !lv_obj_has_flag(timer_run_panel_, LV_OBJ_FLAG_HIDDEN)) {
        // Timer just finished while viewing — switch back to set mode
        if (lbl_timer_countdown_) lv_label_set_text(lbl_timer_countdown_, "Done!");
        // Auto switch back after a moment
        static uint32_t done_time = 0;
        if (done_time == 0) done_time = millis();
        if (millis() - done_time > 2000) {
            done_time = 0;
            show_timer_set_mode();
        }
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
    lbl_sw_time_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_sw_time_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_sw_time_, lv_color_hex(0x44aaff), 0);
    lv_obj_align(lbl_sw_time_, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(lbl_sw_time_, "00:00.00");

    btn_sw_start_ = ui_create_btn(tab, "Start", 80, 30);
    lv_obj_set_pos(btn_sw_start_, 60, 52);
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
    lv_obj_set_pos(btn_sw_lap_, 160, 52);
    lv_obj_add_event_cb(btn_sw_lap_, [](lv_event_t*) {
        if (sw_running_) {
            if (sw_lap_count_ < 20) sw_laps_[sw_lap_count_++] = sw_elapsed();
            if (lbl_sw_laps_) {
                static char lap_buf[200];
                lap_buf[0] = '\0';
                for (int i = sw_lap_count_ - 1; i >= 0 && i >= sw_lap_count_ - 5; i--) {
                    char line[28]; char t[14];
                    fmt_elapsed(sw_laps_[i], t, sizeof(t), true);
                    snprintf(line, sizeof(line), "Lap %d: %s\n", i + 1, t);
                    strncat(lap_buf, line, sizeof(lap_buf) - strlen(lap_buf) - 1);
                }
                lv_label_set_text(lbl_sw_laps_, lap_buf);
            }
        } else {
            sw_accum_ms_ = 0; sw_lap_count_ = 0;
            if (lbl_sw_time_) lv_label_set_text(lbl_sw_time_, "00:00.00");
            if (lbl_sw_laps_) lv_label_set_text(lbl_sw_laps_, "");
        }
    }, LV_EVENT_CLICKED, NULL);

    lbl_sw_laps_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_sw_laps_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_sw_laps_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_sw_laps_, 20, 88);
    lv_label_set_text(lbl_sw_laps_, "");
}

static void update_stopwatch_tab() {
    if (sw_running_ && lbl_sw_time_) {
        char buf[14];
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
        int hr = alert_state_alarm_hour() % 12; if (hr == 0) hr = 12;
        const char* ap = alert_state_alarm_hour() >= 12 ? "PM" : "AM";
        char buf[28];
        snprintf(buf, sizeof(buf), LV_SYMBOL_BELL " %d:%02d %s - ON", hr, alert_state_alarm_minute(), ap);
        lv_label_set_text(lbl_alarm_status_, buf);
        lv_obj_set_style_text_color(lbl_alarm_status_, UI_COLOR_SUCCESS, 0);
    } else {
        lv_label_set_text(lbl_alarm_status_, "Alarm: OFF");
        lv_obj_set_style_text_color(lbl_alarm_status_, UI_COLOR_DIM, 0);
    }
    if (btn_alarm_set_)
        lv_label_set_text(lv_obj_get_child(btn_alarm_set_, 0),
                          alert_state_alarm_enabled() ? "Cancel" : "Set Alarm");
}

static void build_alarm_tab(lv_obj_t* tab) {
    int cx = 150, y = 10;

    lbl_alarm_hh_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_hh_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_alarm_hh_, lv_color_hex(0xe94560), 0);
    lv_obj_set_pos(lbl_alarm_hh_, cx - 80, y);

    lv_obj_t* colon = lv_label_create(tab);
    lv_label_set_text(colon, ":"); lv_obj_set_style_text_font(colon, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(colon, UI_COLOR_DIM, 0); lv_obj_set_pos(colon, cx - 24, y);

    lbl_alarm_mm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_mm_, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_alarm_mm_, lv_color_hex(0xe94560), 0);
    lv_obj_set_pos(lbl_alarm_mm_, cx - 4, y);

    lbl_alarm_ampm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_ampm_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_alarm_ampm_, lv_color_hex(0xf0a500), 0);
    lv_obj_set_pos(lbl_alarm_ampm_, cx + 50, y + 8);

    y += 44;

    auto mk = [&](const char* txt, int x, int by) {
        lv_obj_t* b = ui_create_btn(tab, txt, 32, 24); lv_obj_set_pos(b, x, by); return b;
    };
    lv_obj_t* hh_up = mk("+", cx - 84, y);
    lv_obj_add_event_cb(hh_up, [](lv_event_t*) { alarm_set_hr12_ = (alarm_set_hr12_ % 12) + 1; refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* hh_dn = mk("-", cx - 48, y);
    lv_obj_add_event_cb(hh_dn, [](lv_event_t*) { alarm_set_hr12_ = (alarm_set_hr12_ == 1) ? 12 : alarm_set_hr12_ - 1; refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* mm_up = mk("+", cx - 8, y);
    lv_obj_add_event_cb(mm_up, [](lv_event_t*) { uint8_t m = (alert_state_alarm_minute() + 1) % 60; alert_state_set_alarm(alert_state_alarm_hour(), m); refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* mm_dn = mk("-", cx + 28, y);
    lv_obj_add_event_cb(mm_dn, [](lv_event_t*) { uint8_t m = alert_state_alarm_minute() == 0 ? 59 : alert_state_alarm_minute() - 1; alert_state_set_alarm(alert_state_alarm_hour(), m); refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ampm_btn = ui_create_btn(tab, "AM/PM", 56, 24);
    lv_obj_set_pos(ampm_btn, cx + 66, y);
    lv_obj_add_event_cb(ampm_btn, [](lv_event_t*) { alarm_set_pm_ = !alarm_set_pm_; refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);

    y += 32;

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
    lbl_alarm_status_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_status_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_alarm_status_, cx - 80, y);

    // Init display from persisted state
    int h = alert_state_alarm_hour();
    alarm_set_hr12_ = h % 12; if (alarm_set_hr12_ == 0) alarm_set_hr12_ = 12;
    alarm_set_pm_ = (h >= 12);
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

    // Back button
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

    tabview_ = lv_tabview_create(screen_, LV_DIR_TOP, 28);
    lv_obj_set_size(tabview_, 320, 214);
    lv_obj_set_pos(tabview_, 0, 26);
    lv_obj_set_style_bg_color(tabview_, UI_COLOR_BG, 0);

    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview_);
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(tab_btns, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_PRIMARY, LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t* t1 = lv_tabview_add_tab(tabview_, "Clock");
    lv_obj_t* t2 = lv_tabview_add_tab(tabview_, "Timer");
    lv_obj_t* t3 = lv_tabview_add_tab(tabview_, "Watch");
    lv_obj_t* t4 = lv_tabview_add_tab(tabview_, "Alarm");

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

    // If timer is currently running, show run mode
    if (alert_state_timer_running()) show_timer_run_mode();

    return screen_;
}

void clock_app_update() {
    static uint32_t last = 0;
    if (millis() - last < 100) return;
    last = millis();

    uint16_t active = lv_tabview_get_tab_act(tabview_);
    switch (active) {
        case 0: update_clock_tab(); break;
        case 1: update_timer_tab(); break;
        case 2: update_stopwatch_tab(); break;
        default: break;
    }
}

void clock_app_destroy() {
    screen_ = nullptr; tabview_ = nullptr;
    lbl_clock_time_ = nullptr; lbl_clock_date_ = nullptr; lbl_clock_hijri_ = nullptr; arc_sec_ = nullptr;
    lbl_timer_hh_ = nullptr; lbl_timer_mm_ = nullptr; lbl_timer_ss_ = nullptr;
    lbl_timer_countdown_ = nullptr; btn_timer_start_ = nullptr;
    timer_set_panel_ = nullptr; timer_run_panel_ = nullptr;
    lbl_sw_time_ = nullptr; lbl_sw_laps_ = nullptr; btn_sw_start_ = nullptr; btn_sw_lap_ = nullptr;
    lbl_alarm_hh_ = nullptr; lbl_alarm_mm_ = nullptr; lbl_alarm_ampm_ = nullptr;
    lbl_alarm_status_ = nullptr; btn_alarm_set_ = nullptr;
}
