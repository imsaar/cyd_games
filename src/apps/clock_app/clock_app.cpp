#include "clock_app.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../utils/alert_state.h"
#include "../../net/ntp_time.h"
#include "../../net/weather.h"
#include "../../ui/weather_icons.h"
#include "../../ui/fonts.h"
#include <Arduino.h>
#include <time.h>
#include <math.h>

static lv_obj_t* screen_ = nullptr;
static lv_obj_t* tabview_ = nullptr;
static int active_tab_g = 0;
static lv_obj_t* nav_panels_[5] = {};
static lv_obj_t* nav_btns_g[6] = {};

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

// ── Weather tab ──
static lv_obj_t* lbl_weather_cur_ = nullptr;
static lv_obj_t* lbl_weather_fc_[7] = {};
static lv_obj_t* lbl_weather_status_ = nullptr;
static lv_obj_t* weather_icon_clock_ = nullptr;  // icon on clock face
static lv_obj_t* weather_icons_fc_[7] = {};      // icons in forecast

static void back_cb(lv_event_t*) { screen_manager_back_to_menu(); }
static const char* weather_symbol(int code);  // forward decl

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

static lv_obj_t* lbl_ampm_ = nullptr;
static lv_obj_t* lbl_sec_g_ = nullptr;

static void build_clock_tab(lv_obj_t* tab) {
    // Full 320x196 landscape layout
    // ┌──────────────────────────────────────┐
    // │         12:30          :45  PM       │  <- huge time, seconds, AM/PM
    // │  Wednesday, Apr 15, 2026             │  <- date (full width, 20pt)
    // │  21 Shawwal 1447 AH                  │  <- hijri (16pt amber)
    // │  [sun icon] 62°F Cloudy   Seattle    │  <- weather bar at bottom
    // └──────────────────────────────────────┘

    // Content area: 320 x ~196 (after 44px nav)
    // Layout: 96pt time CENTERED, info in four corners
    //
    // ┌─[Gregorian date]────────────[Seconds arc]─┐
    // │                                            │
    // │              12:30  PM                     │
    // │                                            │
    // └─[Weather icon+temp]──────[Hijri date]─────┘

    // ── CENTER: huge time ──
    lbl_clock_time_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_time_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_style_text_font(lbl_clock_time_, &font_digit_96, 0);
    lv_obj_set_pos(lbl_clock_time_, 20, 30);
    lv_label_set_text(lbl_clock_time_, "12:00");

    // AM/PM right of time center
    lbl_ampm_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_ampm_, lv_color_hex(0xf0a500), 0);
    lv_obj_set_style_text_font(lbl_ampm_, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_ampm_, LV_ALIGN_CENTER, 120, -6);
    lv_label_set_text(lbl_ampm_, "");

    lbl_sec_g_ = nullptr;

    // ── TOP-LEFT: Gregorian date ──
    lbl_clock_date_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_date_, lv_color_hex(0x88bbdd), 0);
    lv_obj_set_style_text_font(lbl_clock_date_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_clock_date_, 4, 2);
    lv_label_set_text(lbl_clock_date_, "");

    // ── TOP-RIGHT: Seconds arc ──
    arc_sec_ = lv_arc_create(tab);
    lv_obj_set_size(arc_sec_, 40, 40);
    lv_obj_set_pos(arc_sec_, 276, 0);
    lv_arc_set_rotation(arc_sec_, 270);
    lv_arc_set_range(arc_sec_, 0, 60);
    lv_arc_set_value(arc_sec_, 0);
    lv_arc_set_bg_angles(arc_sec_, 0, 360);
    lv_obj_set_style_arc_width(arc_sec_, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_sec_, lv_color_hex(0x0f2040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_sec_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_sec_, lv_color_hex(0x4ecca3), LV_PART_INDICATOR);
    lv_obj_remove_style(arc_sec_, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_sec_, LV_OBJ_FLAG_CLICKABLE);

    // ── BOTTOM-LEFT: Weather icon + temp ──
    weather_icon_clock_ = weather_icon_create(tab, 28);
    lv_obj_set_pos(weather_icon_clock_, 4, 134);

    lbl_weather_cur_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_weather_cur_, lv_color_hex(0x66aacc), 0);
    lv_obj_set_style_text_font(lbl_weather_cur_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_weather_cur_, 36, 138);
    lv_label_set_text(lbl_weather_cur_, "");

    // ── BOTTOM-RIGHT: Hijri date ──
    lbl_clock_hijri_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_clock_hijri_, lv_color_hex(0xf0a500), 0);
    lv_obj_set_style_text_font(lbl_clock_hijri_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_clock_hijri_, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_label_set_text(lbl_clock_hijri_, "");
}

static void update_clock_tab() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    int hr = t.tm_hour % 12; if (hr == 0) hr = 12;
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%d:%02d", hr, t.tm_min);
    if (lbl_clock_time_) lv_label_set_text(lbl_clock_time_, tbuf);
    if (lbl_ampm_) lv_label_set_text(lbl_ampm_, t.tm_hour >= 12 ? "PM" : "AM");

    if (arc_sec_) lv_arc_set_value(arc_sec_, t.tm_sec);

    // sec_g_ not used — seconds shown via arc

    static const char* days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char dbuf[32];
    snprintf(dbuf, sizeof(dbuf), "%s, %s %d, %d", days[t.tm_wday], months[t.tm_mon], t.tm_mday, 1900 + t.tm_year);
    if (lbl_clock_date_) lv_label_set_text(lbl_clock_date_, dbuf);

    int hy, hm, hd;
    gregorian_to_hijri(1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, &hy, &hm, &hd);
    char hbuf[32];
    if (hm >= 1 && hm <= 12)
        snprintf(hbuf, sizeof(hbuf), "%d %s %d AH", hd, hijri_month_names[hm - 1], hy);
    else
        hbuf[0] = '\0';
    if (lbl_clock_hijri_) lv_label_set_text(lbl_clock_hijri_, hbuf);

    // Weather icon + text
    const WeatherData* w = weather_get();
    if (w->valid) {
        if (weather_icon_clock_)
            weather_icon_set_code(weather_icon_clock_, w->current_code);
        if (lbl_weather_cur_) {
            char wbuf[32];
            snprintf(wbuf, sizeof(wbuf), "%.0f°F %s",
                     w->current_temp, weather_code_str(w->current_code));
            lv_label_set_text(lbl_weather_cur_, wbuf);
            lv_color_t wc = (w->current_temp > 75) ? lv_color_hex(0xe94560) :
                             (w->current_temp > 60) ? lv_color_hex(0xf0a500) :
                             (w->current_temp > 45) ? lv_color_hex(0x4ecca3) :
                                                       lv_color_hex(0x4488ff);
            lv_obj_set_style_text_color(lbl_weather_cur_, wc, 0);
        }
    }
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
    lv_obj_set_style_text_font(lbl_timer_hh_, &font_digit_72, 0);
    lv_obj_set_style_text_color(lbl_timer_hh_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_pos(lbl_timer_hh_, cx - 130, y);

    lv_obj_t* c1 = lv_label_create(timer_set_panel_);
    lv_label_set_text(c1, ":"); lv_obj_set_style_text_font(c1, &font_digit_72, 0);
    lv_obj_set_style_text_color(c1, UI_COLOR_DIM, 0); lv_obj_set_pos(c1, cx - 68, y);

    // MM
    lbl_timer_mm_ = lv_label_create(timer_set_panel_);
    lv_obj_set_style_text_font(lbl_timer_mm_, &font_digit_72, 0);
    lv_obj_set_style_text_color(lbl_timer_mm_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_pos(lbl_timer_mm_, cx - 46, y);

    lv_obj_t* c2 = lv_label_create(timer_set_panel_);
    lv_label_set_text(c2, ":"); lv_obj_set_style_text_font(c2, &font_digit_72, 0);
    lv_obj_set_style_text_color(c2, UI_COLOR_DIM, 0); lv_obj_set_pos(c2, cx + 16, y);

    // SS
    lbl_timer_ss_ = lv_label_create(timer_set_panel_);
    lv_obj_set_style_text_font(lbl_timer_ss_, &font_digit_72, 0);
    lv_obj_set_style_text_color(lbl_timer_ss_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_pos(lbl_timer_ss_, cx + 38, y);

    y += 60;

    // +/- buttons (larger for touch)
    auto mk_btn = [&](const char* txt, int x, int by) {
        lv_obj_t* b = ui_create_btn(timer_set_panel_, txt, 40, 30);
        lv_obj_set_pos(b, x, by);
        return b;
    };
    lv_obj_t* hh_up = mk_btn("+", cx - 108, y);
    lv_obj_add_event_cb(hh_up, [](lv_event_t*) { if (timer_set_hr_ < 23) timer_set_hr_++; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* hh_dn = mk_btn("-", cx - 64, y);
    lv_obj_add_event_cb(hh_dn, [](lv_event_t*) { if (timer_set_hr_ > 0) timer_set_hr_--; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* mm_up = mk_btn("+", cx - 18, y);
    lv_obj_add_event_cb(mm_up, [](lv_event_t*) { if (timer_set_min_ < 59) timer_set_min_++; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* mm_dn = mk_btn("-", cx + 26, y);
    lv_obj_add_event_cb(mm_dn, [](lv_event_t*) { if (timer_set_min_ > 0) timer_set_min_--; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ss_up = mk_btn("+", cx + 72, y);
    lv_obj_add_event_cb(ss_up, [](lv_event_t*) { if (timer_set_sec_ < 59) timer_set_sec_++; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* ss_dn = mk_btn("-", cx + 116, y);
    lv_obj_add_event_cb(ss_dn, [](lv_event_t*) { if (timer_set_sec_ > 0) timer_set_sec_--; refresh_timer_set(); }, LV_EVENT_CLICKED, NULL);

    // Start button — full width at bottom
    lv_obj_t* start = ui_create_btn(timer_set_panel_, LV_SYMBOL_PLAY " Start", 280, 40);
    lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, -4);
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

    // Massive countdown — 96pt custom font
    lbl_timer_countdown_ = lv_label_create(timer_run_panel_);
    lv_obj_set_style_text_font(lbl_timer_countdown_, &font_digit_96, 0);
    lv_obj_set_style_text_color(lbl_timer_countdown_, lv_color_hex(0xe94560), 0);
    lv_obj_set_style_text_align(lbl_timer_countdown_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_timer_countdown_, 310);
    lv_obj_align(lbl_timer_countdown_, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(lbl_timer_countdown_, "00:00");

    lv_obj_t* cancel = ui_create_btn(timer_run_panel_, LV_SYMBOL_CLOSE " Cancel", 280, 40);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, 0);
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
    lv_obj_set_style_text_font(lbl_sw_time_, &font_digit_72, 0);
    lv_obj_set_style_text_color(lbl_sw_time_, lv_color_hex(0x44aaff), 0);
    lv_label_set_long_mode(lbl_sw_time_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_sw_time_, 320);
    lv_obj_align(lbl_sw_time_, LV_ALIGN_TOP_MID, 0, 20);
    lv_label_set_text(lbl_sw_time_, "00:00.00");

    btn_sw_start_ = ui_create_btn(tab, "Start", 130, 36);
    lv_obj_align(btn_sw_start_, LV_ALIGN_BOTTOM_MID, -70, -6);
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

    btn_sw_lap_ = ui_create_btn(tab, "Reset", 130, 36);
    lv_obj_align(btn_sw_lap_, LV_ALIGN_BOTTOM_MID, 70, -6);
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
    lv_obj_set_style_text_align(lbl_sw_laps_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_sw_laps_, 310);
    lv_obj_align(lbl_sw_laps_, LV_ALIGN_CENTER, 0, 20);
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
    lv_obj_set_style_text_font(lbl_alarm_hh_, &font_digit_72, 0);
    lv_obj_set_style_text_color(lbl_alarm_hh_, lv_color_hex(0xe94560), 0);
    lv_obj_set_pos(lbl_alarm_hh_, cx - 110, y);

    lv_obj_t* colon = lv_label_create(tab);
    lv_label_set_text(colon, ":"); lv_obj_set_style_text_font(colon, &font_digit_72, 0);
    lv_obj_set_style_text_color(colon, UI_COLOR_DIM, 0); lv_obj_set_pos(colon, cx - 48, y);

    lbl_alarm_mm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_mm_, &font_digit_72, 0);
    lv_obj_set_style_text_color(lbl_alarm_mm_, lv_color_hex(0xe94560), 0);
    lv_obj_set_pos(lbl_alarm_mm_, cx - 26, y);

    lbl_alarm_ampm_ = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl_alarm_ampm_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_alarm_ampm_, lv_color_hex(0xf0a500), 0);
    lv_obj_set_pos(lbl_alarm_ampm_, cx + 54, y + 12);

    y += 56;

    auto mk = [&](const char* txt, int x, int by) {
        lv_obj_t* b = ui_create_btn(tab, txt, 40, 30); lv_obj_set_pos(b, x, by); return b;
    };
    lv_obj_t* hh_up = mk("+", cx - 104, y);
    lv_obj_add_event_cb(hh_up, [](lv_event_t*) { alarm_set_hr12_ = (alarm_set_hr12_ % 12) + 1; refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* hh_dn = mk("-", cx - 60, y);
    lv_obj_add_event_cb(hh_dn, [](lv_event_t*) { alarm_set_hr12_ = (alarm_set_hr12_ == 1) ? 12 : alarm_set_hr12_ - 1; refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* mm_up = mk("+", cx - 14, y);
    lv_obj_add_event_cb(mm_up, [](lv_event_t*) { uint8_t m = (alert_state_alarm_minute() + 1) % 60; alert_state_set_alarm(alert_state_alarm_hour(), m); refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t* mm_dn = mk("-", cx + 30, y);
    lv_obj_add_event_cb(mm_dn, [](lv_event_t*) { uint8_t m = alert_state_alarm_minute() == 0 ? 59 : alert_state_alarm_minute() - 1; alert_state_set_alarm(alert_state_alarm_hour(), m); refresh_alarm_display(); }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ampm_btn = ui_create_btn(tab, "AM/PM", 60, 30);
    lv_obj_set_pos(ampm_btn, cx + 76, y);
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
// ══════════════════════════════════════════
// ── Weather tab ──
// ══════════════════════════════════════════

static lv_obj_t* weather_icon_header_ = nullptr;

static void build_weather_tab(lv_obj_t* tab) {
    // Current weather: icon + text header
    weather_icon_header_ = weather_icon_create(tab, 36);
    lv_obj_set_pos(weather_icon_header_, 6, 2);

    lbl_weather_status_ = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_weather_status_, lv_color_hex(0x4ecca3), 0);
    lv_obj_set_style_text_font(lbl_weather_status_, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(lbl_weather_status_, 48, 6);
    lv_label_set_text(lbl_weather_status_, "Seattle Weather");

    // 7-day forecast: large icon | day | temps | condition
    for (int i = 0; i < 7; i++) {
        int y = 40 + i * 22;
        weather_icons_fc_[i] = weather_icon_create(tab, 20);
        lv_obj_set_pos(weather_icons_fc_[i], 4, y);

        lbl_weather_fc_[i] = lv_label_create(tab);
        lv_obj_set_style_text_font(lbl_weather_fc_[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_weather_fc_[i], UI_COLOR_DIM, 0);
        lv_obj_set_pos(lbl_weather_fc_[i], 28, y + 2);
        lv_obj_set_width(lbl_weather_fc_[i], 290);
        lv_label_set_text(lbl_weather_fc_[i], "");
    }
    // Note: 7 rows x 22px = 154px. Max icon size limited by row height.
}

// Text-based weather icons (LVGL built-in fonts have no weather glyphs)
static const char* weather_symbol(int code) {
    if (code == 0) return "SUN";
    if (code <= 3) return "CLD";
    if (code <= 48) return "FOG";
    if (code <= 55) return "DRZ";
    if (code <= 57) return "FRZ";
    if (code <= 65) return "RAN";
    if (code <= 67) return "FRZ";
    if (code <= 77) return "SNW";
    if (code <= 82) return "SHR";
    if (code <= 86) return "SNW";
    return "STM";
}

static void update_weather_tab() {
    const WeatherData* w = weather_get();
    if (!w->valid) {
        if (lbl_weather_status_)
            lv_label_set_text(lbl_weather_status_, "Seattle Weather\nLoading...");
        return;
    }

    if (weather_icon_header_)
        weather_icon_set_code(weather_icon_header_, w->current_code);

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "Seattle  %.0f°F  %s",
             w->current_temp, weather_code_str(w->current_code));
    if (lbl_weather_status_) {
        lv_label_set_text(lbl_weather_status_, hdr);
        // Color current temp
        lv_color_t hc = (w->current_temp > 75) ? lv_color_hex(0xe94560) :
                         (w->current_temp > 60) ? lv_color_hex(0xf0a500) :
                         (w->current_temp > 45) ? lv_color_hex(0x4ecca3) :
                                                   lv_color_hex(0x4488ff);
        lv_obj_set_style_text_color(lbl_weather_status_, hc, 0);
    }

    static const char* day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    struct tm t;
    int wday = 0;
    if (getLocalTime(&t, 0)) wday = t.tm_wday;

    for (int i = 0; i < w->forecast_days && i < 7; i++) {
        if (!lbl_weather_fc_[i]) continue;
        if (weather_icons_fc_[i])
            weather_icon_set_code(weather_icons_fc_[i], w->forecast[i].code);
        const char* dn = (i == 0) ? "Today" : day_names[(wday + i) % 7];
        char buf[52];
        snprintf(buf, sizeof(buf), "%-5s  %3.0f° / %3.0f°F  %s",
                 dn, w->forecast[i].temp_max, w->forecast[i].temp_min,
                 weather_code_str(w->forecast[i].code));
        lv_label_set_text(lbl_weather_fc_[i], buf);

        // Color by high temp
        float hi = w->forecast[i].temp_max;
        lv_color_t c = (hi > 80) ? lv_color_hex(0xe94560) :   // hot = red
                        (hi > 65) ? lv_color_hex(0xf0a500) :   // warm = orange
                        (hi > 50) ? lv_color_hex(0x4ecca3) :   // mild = green
                                    lv_color_hex(0x4488ff);    // cold = blue
        lv_obj_set_style_text_color(lbl_weather_fc_[i], c, 0);
    }
}

// ── Lifecycle ──
// ══════════════════════════════════════════

lv_obj_t* clock_app_create() {
    screen_ = ui_create_screen();
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    // ── Two-row nav bar ──
    // Row 1: Menu, Clock, Timer  |  Row 2: Stopwatch, Alarm, Weather
    static const char* nav_labels[] = {
        LV_SYMBOL_LEFT " Menu", "Clock", "Timer",
        "Stopwatch", "Alarm", "Weather"
    };
    static const int NAV_COUNT = 6;
    static const int ROW_H = 20;
    static const int NAV_H = ROW_H * 2 + 4;
    int col_w[] = {76, 120, 120, 106, 106, 104};  // fill full 320px width
    int row_x[] = {0, 0};  // running x for each row

    memset(nav_btns_g, 0, sizeof(nav_btns_g));
    for (int i = 0; i < NAV_COUNT; i++) {
        int row = (i < 3) ? 0 : 1;
        int y = row * (ROW_H + 2) + 1;
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, col_w[i], ROW_H);
        lv_obj_set_pos(btn, row_x[row] + 2, y);
        row_x[row] += col_w[i] + 2;
        lv_obj_set_style_bg_color(btn, (i == 1) ? UI_COLOR_PRIMARY : UI_COLOR_CARD, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, nav_labels[i]);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        nav_btns_g[i] = btn;
    }

    // Content panels (one per tab, stacked)
    memset(nav_panels_, 0, sizeof(nav_panels_));
    for (int i = 0; i < 5; i++) {
        nav_panels_[i] = lv_obj_create(screen_);
        lv_obj_remove_style_all(nav_panels_[i]);
        lv_obj_set_size(nav_panels_[i], 320, 240 - NAV_H);
        lv_obj_set_pos(nav_panels_[i], 0, NAV_H);
        lv_obj_set_style_bg_color(nav_panels_[i], UI_COLOR_BG, 0);
        lv_obj_set_style_bg_opa(nav_panels_[i], LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(nav_panels_[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(nav_panels_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(nav_panels_[i], 0, 0);
        if (i > 0) lv_obj_add_flag(nav_panels_[i], LV_OBJ_FLAG_HIDDEN);
    }

    active_tab_g = 0;

    // Menu button (index 0)
    lv_obj_add_event_cb(nav_btns_g[0], back_cb, LV_EVENT_CLICKED, NULL);
    // Tab buttons (index 1-5 map to panels 0-4)
    for (int i = 1; i < NAV_COUNT; i++) {
        lv_obj_add_event_cb(nav_btns_g[i], [](lv_event_t* e) {
            int idx = (int)(intptr_t)lv_event_get_user_data(e);
            for (int j = 0; j < 5; j++) {
                if (nav_panels_[j]) {
                    if (j == idx) lv_obj_clear_flag(nav_panels_[j], LV_OBJ_FLAG_HIDDEN);
                    else lv_obj_add_flag(nav_panels_[j], LV_OBJ_FLAG_HIDDEN);
                }
                if (nav_btns_g[j + 1])
                    lv_obj_set_style_bg_color(nav_btns_g[j + 1],
                        (j == idx) ? UI_COLOR_PRIMARY : UI_COLOR_CARD, 0);
            }
            active_tab_g = idx;
        }, LV_EVENT_CLICKED, (void*)(intptr_t)(i - 1));
    }

    build_clock_tab(nav_panels_[0]);
    build_timer_tab(nav_panels_[1]);
    build_stopwatch_tab(nav_panels_[2]);
    build_alarm_tab(nav_panels_[3]);
    build_weather_tab(nav_panels_[4]);

    // If timer is currently running, show run mode
    if (alert_state_timer_running()) show_timer_run_mode();

    return screen_;
}

void clock_app_update() {
    static uint32_t last = 0;
    if (millis() - last < 100) return;
    last = millis();

    switch (active_tab_g) {
        case 0: update_clock_tab(); break;
        case 1: update_timer_tab(); break;
        case 2: update_stopwatch_tab(); break;
        case 4: update_weather_tab(); break;
        default: break;
    }
}

void clock_app_destroy() {
    screen_ = nullptr; tabview_ = nullptr;
    lbl_clock_time_ = nullptr; lbl_clock_date_ = nullptr; lbl_clock_hijri_ = nullptr; arc_sec_ = nullptr; lbl_sec_g_ = nullptr;
    lbl_timer_hh_ = nullptr; lbl_timer_mm_ = nullptr; lbl_timer_ss_ = nullptr;
    lbl_timer_countdown_ = nullptr; btn_timer_start_ = nullptr;
    timer_set_panel_ = nullptr; timer_run_panel_ = nullptr;
    lbl_sw_time_ = nullptr; lbl_sw_laps_ = nullptr; btn_sw_start_ = nullptr; btn_sw_lap_ = nullptr;
    lbl_alarm_hh_ = nullptr; lbl_alarm_mm_ = nullptr; lbl_alarm_ampm_ = nullptr;
    lbl_alarm_status_ = nullptr; btn_alarm_set_ = nullptr;
    lbl_weather_cur_ = nullptr; lbl_weather_status_ = nullptr;
    weather_icon_clock_ = nullptr; weather_icon_header_ = nullptr;
    memset(lbl_weather_fc_, 0, sizeof(lbl_weather_fc_));
    memset(weather_icons_fc_, 0, sizeof(weather_icons_fc_));
}
