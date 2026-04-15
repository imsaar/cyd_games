#include "pictionary.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

// ── Word bank ──
const char* const Pictionary::words_[] = {
    "sun", "moon", "star", "tree", "flower", "house", "car", "fish",
    "bird", "cat", "dog", "hat", "shoe", "ball", "book", "cup",
    "key", "door", "clock", "heart", "cloud", "rain", "snow", "fire",
    "boat", "train", "plane", "bike", "apple", "banana", "pizza", "cake",
    "chair", "table", "lamp", "bed", "phone", "guitar", "drum", "bell",
    "eye", "hand", "foot", "ear", "nose", "mouth", "tooth", "bone",
    "snake", "spider", "bee", "ant", "frog", "mouse", "pig", "cow",
    "horse", "duck", "egg", "cheese", "bread", "ice cream", "candy", "cookie",
    "sword", "crown", "ring", "flag", "kite", "balloon", "rocket", "robot",
    "ghost", "skull", "pumpkin", "snowman", "rainbow", "mountain", "island", "volcano",
    "bridge", "castle", "tent", "ladder", "anchor", "umbrella", "glasses", "camera",
    "pencil", "scissors", "hammer", "wrench", "candle", "diamond", "arrow", "lightning"
};
const int Pictionary::WORD_COUNT = sizeof(words_) / sizeof(words_[0]);

// ── Drawing geometry ──
static const int DRAW_X = 0;
static const int DRAW_Y = 0;
static const int DRAW_W = 240;
static const int DRAW_H = 200;
static const int TOOLBAR_Y = 202;

// ── Color palette ──
static const uint32_t palette_hex[Pictionary::NUM_COLORS] = {
    0xFFFFFF, 0xFF4444, 0x44CC44, 0x4488FF, 0xFFDD00, 0xFF88FF
};
static lv_color_t palette_color(int idx) {
    return lv_color_hex(palette_hex[idx % Pictionary::NUM_COLORS]);
}

// ── Static self pointer for discovery callbacks ──
static Pictionary* s_self = nullptr;
static IPAddress pending_invite_ip;
static lv_obj_t* invite_msgbox = nullptr;

static void back_cb(lv_event_t*) { screen_manager_back_to_menu(); }

// ── Discovery callbacks ──

void pict_on_invite(const Peer& from) {
    if (!s_self || s_self->phase_ != Pictionary::PHASE_LOBBY) return;
    if (invite_msgbox) return;

    pending_invite_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    invite_msgbox = lv_msgbox_create(NULL, "Pictionary Invite", from.name, btns, false);
    lv_obj_set_size(invite_msgbox, 240, 140);
    lv_obj_center(invite_msgbox);
    lv_obj_set_style_bg_color(invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(invite_msgbox, UI_COLOR_TEXT, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t*) {
        uint16_t btn_id = lv_msgbox_get_active_btn(invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(pending_invite_ip);
            s_self->peer_ip_ = pending_invite_ip;
            s_self->is_host_ = false;
            s_self->network_mode_ = true;
            discovery_set_game("pictionary", "playing");
            lv_msgbox_close(invite_msgbox);
            invite_msgbox = nullptr;
            // Guest waits for host to send setup
            s_self->clear_ui();
            lv_obj_t* lbl = lv_label_create(s_self->screen_);
            lv_label_set_text(lbl, "Waiting for host to start...");
            lv_obj_set_style_text_color(lbl, UI_COLOR_DIM, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_center(lbl);
        } else {
            lv_msgbox_close(invite_msgbox);
            invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void pict_on_accept(const Peer& from) {
    if (!s_self || s_self->phase_ != Pictionary::PHASE_LOBBY) return;
    s_self->peer_ip_ = from.ip;
    s_self->is_host_ = true;
    s_self->network_mode_ = true;
    discovery_set_game("pictionary", "playing");
    // Host starts the game
    s_self->score_[0] = 0;
    s_self->score_[1] = 0;
    s_self->round_ = 0;
    s_self->drawer_ = 0;
    s_self->start_round();
}

void pict_on_game_data(const char* json) {
    if (!s_self || !s_self->network_mode_) return;
    s_self->onNetworkData(json);
}

void pict_lobby_peer_cb(lv_event_t* e) {
    if (!s_self) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const Peer* peers = discovery_get_peers();
    int count = discovery_peer_count();
    if (idx < 0 || idx >= count) return;
    discovery_send_invite(peers[idx].ip);
    if (s_self->lobby_list_) {
        lv_obj_clean(s_self->lobby_list_);
        lv_list_add_text(s_self->lobby_list_, "Invite sent, waiting...");
    }
}

// ── Helpers ──

void Pictionary::pick_word() {
    word_idx_ = esp_random() % WORD_COUNT;
    correct_choice_ = esp_random() % 4;
    bool used[WORD_COUNT];
    memset(used, 0, sizeof(used));
    used[word_idx_] = true;
    for (int i = 0; i < 4; i++) {
        if (i == correct_choice_) {
            choices_[i] = word_idx_;
        } else {
            int idx;
            do { idx = esp_random() % WORD_COUNT; } while (used[idx]);
            used[idx] = true;
            choices_[i] = idx;
        }
    }
}

void Pictionary::send_json(const char* buf) {
    discovery_send_game_data(peer_ip_, buf);
}

// ── Hex encoding for compact point transport ──
// Each point = 3 bytes (x, y, color) = 6 hex chars
// Separator (-1,-1) encoded as (0xFF, 0xFF, 0x00)
// 30 points per chunk = 180 hex chars — fits in 250-byte packet

static const char hex_chars[] = "0123456789ABCDEF";

static void pts_to_hex(Pictionary::Pt* pts, int start, int count, char* out) {
    for (int i = 0; i < count; i++) {
        auto& pt = pts[start + i];
        uint8_t xb = (pt.x < 0) ? 0xFF : (uint8_t)pt.x;
        uint8_t yb = (pt.y < 0) ? 0xFF : (uint8_t)pt.y;
        uint8_t cb = pt.color;
        int o = i * 6;
        out[o]   = hex_chars[xb >> 4]; out[o+1] = hex_chars[xb & 0xF];
        out[o+2] = hex_chars[yb >> 4]; out[o+3] = hex_chars[yb & 0xF];
        out[o+4] = hex_chars[cb >> 4]; out[o+5] = hex_chars[cb & 0xF];
    }
    out[count * 6] = '\0';
}

static uint8_t hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static void hex_to_pts(const char* hex, Pictionary::Pt* pts, int start, int count) {
    for (int i = 0; i < count; i++) {
        int o = i * 6;
        uint8_t xb = (hex_val(hex[o]) << 4) | hex_val(hex[o+1]);
        uint8_t yb = (hex_val(hex[o+2]) << 4) | hex_val(hex[o+3]);
        uint8_t cb = (hex_val(hex[o+4]) << 4) | hex_val(hex[o+5]);
        int16_t x = (xb == 0xFF) ? -1 : (int16_t)xb;
        int16_t y = (yb == 0xFF) ? -1 : (int16_t)yb;
        pts[start + i] = {x, y, cb};
    }
}

static const int PTS_PER_CHUNK = 28;  // 28 pts × 6 hex = 168 chars, fits in packet

void Pictionary::send_full_sync() {
    // Send all points as hex-encoded chunks with offsets.
    // Each chunk writes to a specific position, so lost packets
    // only cause gaps rather than corrupting the entire drawing.
    int sent = 0;
    while (sent < pt_count_) {
        int batch = pt_count_ - sent;
        if (batch > PTS_PER_CHUNK) batch = PTS_PER_CHUNK;

        char hex[PTS_PER_CHUNK * 6 + 1];
        pts_to_hex(pts_, sent, batch, hex);

        // {"type":"move","game":"pictionary","a":"f","o":0,"t":100,"h":"..."}
        char buf[250];
        snprintf(buf, sizeof(buf),
            "{\"type\":\"move\",\"game\":\"pictionary\",\"a\":\"f\",\"o\":%d,\"t\":%d,\"h\":\"%s\"}",
            sent, pt_count_, hex);
        send_json(buf);
        sent += batch;
    }
    last_sent_pt_ = pt_count_;
}

void Pictionary::send_strokes() {
    if (last_sent_pt_ >= pt_count_) return;
    int batch = pt_count_ - last_sent_pt_;
    if (batch > PTS_PER_CHUNK) batch = PTS_PER_CHUNK;

    char hex[PTS_PER_CHUNK * 6 + 1];
    pts_to_hex(pts_, last_sent_pt_, batch, hex);

    // Incremental: append on receiver side
    char buf[250];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"move\",\"game\":\"pictionary\",\"a\":\"i\",\"h\":\"%s\"}",
        hex);
    send_json(buf);
    last_sent_pt_ += batch;
}

// ── Custom draw callback ──
void Pictionary::draw_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.width = 3;
    dsc.round_start = 1;
    dsc.round_end = 1;

    for (int i = 1; i < self->pt_count_; i++) {
        if (self->pts_[i-1].x < 0 || self->pts_[i].x < 0) continue;
        dsc.color = palette_color(self->pts_[i].color);
        lv_point_t p1 = {
            (lv_coord_t)(a.x1 + self->pts_[i-1].x),
            (lv_coord_t)(a.y1 + self->pts_[i-1].y)
        };
        lv_point_t p2 = {
            (lv_coord_t)(a.x1 + self->pts_[i].x),
            (lv_coord_t)(a.y1 + self->pts_[i].y)
        };
        lv_draw_line(draw_ctx, &dsc, &p1, &p2);
    }
}

// ── UI event callbacks ──

void Pictionary::start_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->network_mode_ = false;
    self->score_[0] = 0;
    self->score_[1] = 0;
    self->round_ = 0;
    self->drawer_ = 0;
    self->start_round();
}

void Pictionary::reveal_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->start_drawing();
}

void Pictionary::done_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    if (self->network_mode_) {
        // Send full drawing sync then done signal
        self->send_full_sync();
        StaticJsonDocument<96> doc;
        doc["type"] = "move"; doc["game"] = "pictionary";
        doc["a"] = "done";
        char buf[96];
        serializeJson(doc, buf, sizeof(buf));
        self->send_json(buf);
        self->show_wait_guess();
    } else {
        self->start_guessing();
    }
}

void Pictionary::clear_strokes_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->pt_count_ = 0;
    self->last_sent_pt_ = 0;
    if (self->draw_area_) lv_obj_invalidate(self->draw_area_);
    if (self->network_mode_) {
        StaticJsonDocument<96> doc;
        doc["type"] = "move"; doc["game"] = "pictionary";
        doc["a"] = "clr";
        char buf[96];
        serializeJson(doc, buf, sizeof(buf));
        self->send_json(buf);
    }
}

void Pictionary::color_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    self->cur_color_ = (uint8_t)idx;
}

void Pictionary::choice_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    int picked = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    bool correct = (picked == self->correct_choice_);
    bool early = (self->phase_ == Pictionary::PHASE_WATCH);

    if (correct) {
        int guesser = self->drawer_ ^ 1;
        self->score_[guesser]++;
        if (early) self->score_[guesser]++;  // bonus for early guess
    }
    self->guessed_early_ = early;

    if (self->network_mode_) {
        // Send guess to drawer
        StaticJsonDocument<96> doc;
        doc["type"] = "move"; doc["game"] = "pictionary";
        doc["a"] = "guess";
        doc["c"] = picked;
        doc["ok"] = correct;
        doc["early"] = early;
        char buf[96];
        serializeJson(doc, buf, sizeof(buf));
        self->send_json(buf);
    }

    self->show_result(correct);
}

void Pictionary::next_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->round_++;
    self->drawer_ ^= 1;
    if (self->round_ >= TOTAL_ROUNDS) {
        if (self->network_mode_ && self->is_host_) {
            StaticJsonDocument<96> doc;
            doc["type"] = "move"; doc["game"] = "pictionary";
            doc["a"] = "over";
            doc["s0"] = self->score_[0];
            doc["s1"] = self->score_[1];
            char buf[96];
            serializeJson(doc, buf, sizeof(buf));
            self->send_json(buf);
        }
        self->show_gameover();
    } else {
        self->start_round();
    }
}

void Pictionary::play_again_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->show_menu();
}

void Pictionary::mode_local_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->network_mode_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    self->show_menu();
}

void Pictionary::mode_network_cb(lv_event_t* e) {
    Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
    self->show_lobby();
}

// ── Screen lifecycle ──

lv_obj_t* Pictionary::createScreen() {
    s_self = this;
    invite_msgbox = nullptr;
    screen_ = ui_create_screen();
    phase_ = PHASE_MENU;
    show_mode_select();
    return screen_;
}

void Pictionary::update() {
    // Lobby peer refresh
    if (phase_ == PHASE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);
            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "pictionary") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)", peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, pict_lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }

    // Drawing phase
    if (phase_ != PHASE_DRAW) return;

    uint32_t elapsed = (millis() - draw_start_) / 1000;
    int remaining = DRAW_TIME - (int)elapsed;
    if (remaining < 0) remaining = 0;

    if (lbl_timer_) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", remaining);
        lv_label_set_text(lbl_timer_, buf);
    }

    if (remaining <= 0) {
        if (network_mode_) {
            send_full_sync();
            StaticJsonDocument<96> doc;
            doc["type"] = "move"; doc["game"] = "pictionary";
            doc["a"] = "done";
            char buf[96];
            serializeJson(doc, buf, sizeof(buf));
            send_json(buf);
            show_wait_guess();
        } else {
            start_guessing();
        }
        return;
    }

    // Send strokes periodically in network mode
    if (network_mode_ && millis() - last_send_time_ > 200) {
        last_send_time_ = millis();
        send_strokes();
    }

    // Full sync every 3 seconds to recover dropped packets
    static uint32_t last_full_sync = 0;
    if (network_mode_ && pt_count_ > 0 && millis() - last_full_sync > 3000) {
        last_full_sync = millis();
        send_full_sync();
    }

    // Track touch input for drawing
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) {
        indev = lv_indev_get_next(NULL);
        while (indev && lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            indev = lv_indev_get_next(indev);
        }
    }
    if (!indev) return;

    bool pressed = (indev->proc.state == LV_INDEV_STATE_PRESSED);
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int lx = p.x - DRAW_X;
    int ly = p.y - DRAW_Y;
    bool in_bounds = (lx >= 0 && lx < DRAW_W && ly >= 0 && ly < DRAW_H);

    if (pressed && in_bounds) {
        if (!pen_down_ && pt_count_ > 0 && pt_count_ < MAX_PTS) {
            pts_[pt_count_++] = {-1, -1, 0};
        }
        pen_down_ = true;
        if (pt_count_ < MAX_PTS) {
            pts_[pt_count_++] = {(int16_t)lx, (int16_t)ly, cur_color_};
            if (draw_area_) lv_obj_invalidate(draw_area_);
        }
    } else {
        pen_down_ = false;
    }
}

void Pictionary::destroy() {
    if (invite_msgbox) {
        lv_msgbox_close(invite_msgbox);
        invite_msgbox = nullptr;
    }
    if (network_mode_) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"pictionary\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    draw_area_ = nullptr;
    lbl_info_ = nullptr;
    lbl_timer_ = nullptr;
    lbl_score_ = nullptr;
    btn_panel_ = nullptr;
    lobby_list_ = nullptr;
    memset(choice_btns_, 0, sizeof(choice_btns_));
}

// ── Network data handling ──

void Pictionary::onNetworkData(const char* json) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "pictionary") != 0) return;
    const char* action = doc["a"];
    if (!action) return;
    if (doc["abandon"] | false) {
        show_gameover();
        return;
    }

    if (strcmp(action, "setup") == 0) {
        // Guest receives round setup from host
        word_idx_ = doc["w"] | 0;
        correct_choice_ = doc["cc"] | 0;
        JsonArray ca = doc["ch"];
        for (int i = 0; i < 4 && i < (int)ca.size(); i++) choices_[i] = ca[i];
        round_ = doc["r"] | 0;
        drawer_ = doc["d"] | 0;
        score_[0] = doc["s0"] | 0;
        score_[1] = doc["s1"] | 0;

        // Determine role: host=0, guest=1. drawer_ says who draws.
        is_drawer_ = (is_host_ ? drawer_ == 0 : drawer_ == 1);
        pt_count_ = 0;
        last_sent_pt_ = 0;
        pen_down_ = false;
        cur_color_ = 0;
        guessed_early_ = false;

        if (is_drawer_) {
            show_reveal();
        } else {
            start_watching();
        }
    }
    else if (strcmp(action, "f") == 0) {
        // Full sync chunk: write points at specific offset
        int offset = doc["o"] | 0;
        int total = doc["t"] | 0;
        const char* hex = doc["h"];
        if (!hex || total <= 0 || total > MAX_PTS) return;
        int hex_len = strlen(hex);
        int count = hex_len / 6;
        if (offset + count > MAX_PTS) count = MAX_PTS - offset;
        if (count > 0) {
            hex_to_pts(hex, pts_, offset, count);
        }
        pt_count_ = total;  // trust drawer's total count
        if (draw_area_) lv_obj_invalidate(draw_area_);
    }
    else if (strcmp(action, "i") == 0) {
        // Incremental stroke: append to end of buffer
        const char* hex = doc["h"];
        if (!hex) return;
        int hex_len = strlen(hex);
        int count = hex_len / 6;
        if (pt_count_ + count > MAX_PTS) count = MAX_PTS - pt_count_;
        if (count > 0) {
            hex_to_pts(hex, pts_, pt_count_, count);
            pt_count_ += count;
        }
        if (draw_area_) lv_obj_invalidate(draw_area_);
    }
    else if (strcmp(action, "clr") == 0) {
        pt_count_ = 0;
        if (draw_area_) lv_obj_invalidate(draw_area_);
    }
    else if (strcmp(action, "done") == 0) {
        // Drawer is done — if guesser hasn't guessed, force guess phase
        if (phase_ == PHASE_WATCH) {
            // Rebuild as pure guess screen (remove timer, show "Time's up!")
            start_guessing();
        }
    }
    else if (strcmp(action, "guess") == 0) {
        // Drawer receives guess from remote guesser
        bool correct = doc["ok"] | false;
        bool early = doc["early"] | false;
        if (correct) {
            int guesser = drawer_ ^ 1;
            score_[guesser]++;
            if (early) score_[guesser]++;
        }
        guessed_early_ = early;
        show_result(correct);
    }
    else if (strcmp(action, "next") == 0) {
        // Sync round progression from host
        round_ = doc["r"] | (round_ + 1);
        drawer_ = doc["d"] | (drawer_ ^ 1);
        score_[0] = doc["s0"] | score_[0];
        score_[1] = doc["s1"] | score_[1];
        if (round_ >= TOTAL_ROUNDS) {
            show_gameover();
        } else {
            is_drawer_ = (is_host_ ? drawer_ == 0 : drawer_ == 1);
            pt_count_ = 0;
            last_sent_pt_ = 0;
            pen_down_ = false;
            cur_color_ = 0;
            guessed_early_ = false;
            if (is_drawer_) {
                pick_word();
                show_reveal();
                // Send setup to peer
                StaticJsonDocument<192> sdoc;
                sdoc["type"] = "move"; sdoc["game"] = "pictionary";
                sdoc["a"] = "setup";
                sdoc["w"] = word_idx_;
                sdoc["cc"] = correct_choice_;
                JsonArray ca = sdoc.createNestedArray("ch");
                for (int i = 0; i < 4; i++) ca.add(choices_[i]);
                sdoc["r"] = round_;
                sdoc["d"] = drawer_;
                sdoc["s0"] = score_[0];
                sdoc["s1"] = score_[1];
                char buf[192];
                serializeJson(sdoc, buf, sizeof(buf));
                send_json(buf);
            }
            // Non-drawer waits for "setup" message
        }
    }
    else if (strcmp(action, "over") == 0) {
        score_[0] = doc["s0"] | score_[0];
        score_[1] = doc["s1"] | score_[1];
        show_gameover();
    }
}

// ── Phase UI builders ──

void Pictionary::clear_ui() {
    lv_obj_clean(screen_);
    draw_area_ = nullptr;
    lbl_info_ = nullptr;
    lbl_timer_ = nullptr;
    lbl_score_ = nullptr;
    btn_panel_ = nullptr;
    lobby_list_ = nullptr;
    memset(choice_btns_, 0, sizeof(choice_btns_));
}

void Pictionary::show_menu() {
    clear_ui();
    phase_ = PHASE_MENU;
    network_mode_ = false;

    ui_create_back_btn(screen_);
    lv_obj_t* title = ui_create_title(screen_, "Pictionary");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* desc = lv_label_create(screen_);
    lv_label_set_text(desc, "One player draws, the other guesses!\n"
                            "Take turns over 6 rounds.\n"
                            "Pass the device between turns.");
    lv_obj_set_style_text_color(desc, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(desc, 280);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* btn = ui_create_btn(screen_, LV_SYMBOL_PLAY " Start Game", 180, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_event_cb(btn, start_cb, LV_EVENT_CLICKED, this);
}

void Pictionary::show_mode_select() {
    clear_ui();
    phase_ = PHASE_MODE_SELECT;

    ui_create_back_btn(screen_);
    lv_obj_t* title = ui_create_title(screen_, "Pictionary");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* btn_local = ui_create_btn(screen_, "Local (2P)", 140, 50);
    lv_obj_align(btn_local, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn_local, start_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* btn_net = ui_create_btn(screen_, "Network (2P)", 140, 50);
    lv_obj_align(btn_net, LV_ALIGN_CENTER, 0, 35);
    lv_obj_add_event_cb(btn_net, mode_network_cb, LV_EVENT_CLICKED, this);
}

void Pictionary::show_lobby() {
    clear_ui();
    phase_ = PHASE_LOBBY;

    discovery_set_game("pictionary", "waiting");
    discovery_on_invite(pict_on_invite);
    discovery_on_accept(pict_on_accept);
    discovery_on_game_data(pict_on_game_data);

    ui_create_back_btn(screen_);
    lv_obj_t* title = ui_create_title(screen_, "Finding Opponents...");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lobby_list_ = lv_list_create(screen_);
    lv_obj_set_size(lobby_list_, 280, 160);
    lv_obj_align(lobby_list_, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(lobby_list_, UI_COLOR_CARD, 0);

    lv_obj_t* hint = lv_label_create(screen_);
    lv_label_set_text(hint, "Tap a peer to invite");
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
}

void Pictionary::start_round() {
    pick_word();
    pt_count_ = 0;
    last_sent_pt_ = 0;
    pen_down_ = false;
    cur_color_ = 0;
    guessed_early_ = false;

    if (network_mode_) {
        is_drawer_ = (is_host_ ? drawer_ == 0 : drawer_ == 1);

        // Whoever is drawer sends setup to peer
        if (is_drawer_) {
            StaticJsonDocument<192> doc;
            doc["type"] = "move"; doc["game"] = "pictionary";
            doc["a"] = "setup";
            doc["w"] = word_idx_;
            doc["cc"] = correct_choice_;
            JsonArray ca = doc.createNestedArray("ch");
            for (int i = 0; i < 4; i++) ca.add(choices_[i]);
            doc["r"] = round_;
            doc["d"] = drawer_;
            doc["s0"] = score_[0];
            doc["s1"] = score_[1];
            char buf[192];
            serializeJson(doc, buf, sizeof(buf));
            send_json(buf);
            show_reveal();
        } else {
            start_watching();
        }
    } else {
        show_reveal();
    }
}

void Pictionary::show_reveal() {
    clear_ui();
    phase_ = PHASE_REVEAL;

    lbl_score_ = lv_label_create(screen_);
    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "P1: %d  |  P2: %d", score_[0], score_[1]);
    lv_label_set_text(lbl_score_, sbuf);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_RIGHT, -8, 6);

    lv_obj_t* round_lbl = lv_label_create(screen_);
    char rbuf[24];
    snprintf(rbuf, sizeof(rbuf), "Round %d/%d", round_ + 1, TOTAL_ROUNDS);
    lv_label_set_text(round_lbl, rbuf);
    lv_obj_set_style_text_color(round_lbl, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(round_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(round_lbl, LV_ALIGN_TOP_LEFT, 8, 6);

    const char* who_text = network_mode_ ? "You draw!" : (drawer_ == 0 ? "Player 1 draws!" : "Player 2 draws!");
    lv_obj_t* who = lv_label_create(screen_);
    lv_label_set_text(who, who_text);
    lv_obj_set_style_text_color(who, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(who, &lv_font_montserrat_20, 0);
    lv_obj_align(who, LV_ALIGN_CENTER, 0, -40);

    // Masked word — tap to reveal
    lv_obj_t* mask_btn = lv_btn_create(screen_);
    lv_obj_set_size(mask_btn, 220, 44);
    lv_obj_align(mask_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(mask_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(mask_btn, 8, 0);
    lv_obj_set_style_border_color(mask_btn, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_border_width(mask_btn, 1, 0);

    lv_obj_t* mask_lbl = lv_label_create(mask_btn);
    lv_label_set_text(mask_lbl, LV_SYMBOL_EYE_CLOSE " Tap to reveal word");
    lv_obj_set_style_text_color(mask_lbl, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(mask_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(mask_lbl);

    struct RevealData { Pictionary* self; lv_obj_t* btn; lv_obj_t* lbl; };
    static RevealData rd;
    rd = { this, mask_btn, mask_lbl };
    lv_obj_add_event_cb(mask_btn, [](lv_event_t* e) {
        RevealData* d = (RevealData*)lv_event_get_user_data(e);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s", d->self->words_[d->self->word_idx_]);
        lv_label_set_text(d->lbl, buf);
        lv_obj_set_style_text_color(d->lbl, UI_COLOR_SUCCESS, 0);
        lv_obj_set_style_text_font(d->lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_bg_color(d->btn, lv_color_hex(0x0a2a10), 0);
    }, LV_EVENT_CLICKED, &rd);

    lv_obj_t* hint = lv_label_create(screen_);
    lv_label_set_text(hint, network_mode_ ? "Your opponent can't see this" : "Don't let the guesser see!");
    lv_obj_set_style_text_color(hint, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 34);

    lv_obj_t* btn = ui_create_btn(screen_, "Ready to Draw", 160, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, reveal_cb, LV_EVENT_CLICKED, this);
}

// Helper: create the draw area object
lv_obj_t* create_draw_area(lv_obj_t* parent, Pictionary* self) {
    lv_obj_t* area = lv_obj_create(parent);
    lv_obj_set_size(area, DRAW_W, DRAW_H);
    lv_obj_set_pos(area, DRAW_X, DRAW_Y);
    lv_obj_set_style_bg_color(area, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(area, 0, 0);
    lv_obj_set_style_border_color(area, lv_color_hex(0x334466), 0);
    lv_obj_set_style_border_width(area, 1, 0);
    lv_obj_set_style_pad_all(area, 0, 0);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(area, Pictionary::draw_cb, LV_EVENT_DRAW_POST, self);
    return area;
}

void Pictionary::start_drawing() {
    clear_ui();
    phase_ = PHASE_DRAW;
    draw_start_ = millis();
    last_send_time_ = millis();

    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    draw_area_ = create_draw_area(screen_, this);

    int tb_x = DRAW_W + 4;
    int tb_w = 320 - DRAW_W - 8;
    int y = 4;

    lbl_timer_ = lv_label_create(screen_);
    lv_label_set_text(lbl_timer_, "30s");
    lv_obj_set_style_text_color(lbl_timer_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_timer_, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_timer_, tb_x + 4, y);

    lbl_score_ = lv_label_create(screen_);
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "R%d %d-%d", round_ + 1, score_[0], score_[1]);
    lv_label_set_text(lbl_score_, sbuf);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_score_, tb_x + 42, y + 2);
    y += 22;

    for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % 3, row = i / 3;
        lv_obj_t* swatch = lv_btn_create(screen_);
        lv_obj_set_size(swatch, 22, 18);
        lv_obj_set_pos(swatch, tb_x + col * 25, y + row * 21);
        lv_obj_set_style_bg_color(swatch, palette_color(i), 0);
        lv_obj_set_style_radius(swatch, 3, 0);
        lv_obj_set_style_border_width(swatch, (i == cur_color_) ? 2 : 0, 0);
        lv_obj_set_style_border_color(swatch, lv_color_white(), 0);
        lv_obj_set_style_shadow_width(swatch, 0, 0);
        lv_obj_set_user_data(swatch, (void*)(intptr_t)i);
        lv_obj_add_event_cb(swatch, color_cb, LV_EVENT_CLICKED, this);
    }
    y += 46;

    lv_obj_t* clr_btn = ui_create_btn(screen_, "Clear", tb_w, 26);
    lv_obj_set_pos(clr_btn, tb_x, y);
    lv_obj_add_event_cb(clr_btn, clear_strokes_cb, LV_EVENT_CLICKED, this);
    y += 30;

    lv_obj_t* done_btn = ui_create_btn(screen_, "Done", tb_w, 26);
    lv_obj_set_pos(done_btn, tb_x, y);
    lv_obj_add_event_cb(done_btn, done_cb, LV_EVENT_CLICKED, this);
    y += 30;

    lv_obj_t* hint = lv_label_create(screen_);
    char hbuf[40];
    snprintf(hbuf, sizeof(hbuf), "Draw \"%s\"", words_[word_idx_]);
    lv_label_set_text(hint, hbuf);
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(hint, 4, TOOLBAR_Y + 16);

    // Exit button — bottom right
    lv_obj_t* exit_btn = ui_create_btn(screen_, "Exit", 56, 24);
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_add_event_cb(exit_btn, back_cb, LV_EVENT_CLICKED, NULL);
}

// Network guesser: watch drawing live with guess buttons visible
void Pictionary::start_watching() {
    clear_ui();
    phase_ = PHASE_WATCH;

    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    draw_area_ = create_draw_area(screen_, this);

    // Bottom info + exit
    lv_obj_t* prompt = lv_label_create(screen_);
    lv_label_set_text(prompt, "Guess now for bonus point!");
    lv_obj_set_style_text_color(prompt, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(prompt, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(prompt, 4, DRAW_H + 4);

    lbl_score_ = lv_label_create(screen_);
    char sbuf[20];
    snprintf(sbuf, sizeof(sbuf), "R%d  %d-%d", round_ + 1, score_[0], score_[1]);
    lv_label_set_text(lbl_score_, sbuf);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_score_, 4, DRAW_H + 20);

    // 4 choice buttons on the right
    int tb_x = DRAW_W + 4;
    int tb_w = 320 - DRAW_W - 8;
    int btn_h = 44;
    int gap = 6;
    int start_y = 4;

    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, tb_w, btn_h);
        lv_obj_set_pos(btn, tb_x, start_y + i * (btn_h + gap));
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_left(btn, 4, 0);
        lv_obj_set_style_pad_right(btn, 4, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, words_[choices_[i]]);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(lbl, tb_w - 8);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, choice_cb, LV_EVENT_CLICKED, this);
        choice_btns_[i] = btn;
    }

    // Exit — bottom right
    lv_obj_t* exit_btn = ui_create_btn(screen_, "Exit", 56, 24);
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_add_event_cb(exit_btn, back_cb, LV_EVENT_CLICKED, NULL);
}

void Pictionary::start_guessing() {
    // Save strokes across UI rebuild
    Pt saved[MAX_PTS];
    int saved_count = pt_count_;
    memcpy(saved, pts_, pt_count_ * sizeof(Pt));

    clear_ui();
    phase_ = PHASE_GUESS;

    draw_area_ = create_draw_area(screen_, this);
    memcpy(pts_, saved, saved_count * sizeof(Pt));
    pt_count_ = saved_count;

    lv_obj_t* prompt = lv_label_create(screen_);
    if (network_mode_) {
        lv_label_set_text(prompt, "Drawer is done - guess now!");
    } else {
        char pbuf[40];
        snprintf(pbuf, sizeof(pbuf), "P%d - Guess!", (drawer_ ^ 1) + 1);
        lv_label_set_text(prompt, pbuf);
    }
    lv_obj_set_style_text_color(prompt, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(prompt, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(prompt, 4, DRAW_H + 4);

    lbl_score_ = lv_label_create(screen_);
    char sbuf[20];
    snprintf(sbuf, sizeof(sbuf), "P1:%d P2:%d", score_[0], score_[1]);
    lv_label_set_text(lbl_score_, sbuf);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_score_, 4, DRAW_H + 20);

    int tb_x = DRAW_W + 4;
    int tb_w = 320 - DRAW_W - 8;
    int btn_h = 44;
    int gap = 6;
    int start_y = 4;

    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, tb_w, btn_h);
        lv_obj_set_pos(btn, tb_x, start_y + i * (btn_h + gap));
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_left(btn, 4, 0);
        lv_obj_set_style_pad_right(btn, 4, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, words_[choices_[i]]);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(lbl, tb_w - 8);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
        lv_obj_center(lbl);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, choice_cb, LV_EVENT_CLICKED, this);
        choice_btns_[i] = btn;
    }

    // Exit — bottom right
    lv_obj_t* exit_btn = ui_create_btn(screen_, "Exit", 56, 24);
    lv_obj_align(exit_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    lv_obj_add_event_cb(exit_btn, back_cb, LV_EVENT_CLICKED, NULL);
}

void Pictionary::show_wait_guess() {
    clear_ui();
    phase_ = PHASE_WAIT_GUESS;

    draw_area_ = create_draw_area(screen_, this);

    lv_obj_t* lbl = lv_label_create(screen_);
    lv_label_set_text(lbl, "Waiting for\nguess...");
    lv_obj_set_style_text_color(lbl, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl, DRAW_W + 8, 80);

    lv_obj_t* exit_btn = ui_create_btn(screen_, "Exit", 60, 26);
    lv_obj_set_pos(exit_btn, DRAW_W + 10, 140);
    lv_obj_add_event_cb(exit_btn, back_cb, LV_EVENT_CLICKED, NULL);
}

void Pictionary::show_result(bool correct) {
    clear_ui();
    phase_ = PHASE_RESULT;

    draw_area_ = lv_obj_create(screen_);
    lv_obj_set_size(draw_area_, 180, 140);
    lv_obj_align(draw_area_, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(draw_area_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(draw_area_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(draw_area_, 6, 0);
    lv_obj_set_style_border_color(draw_area_, correct ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(draw_area_, 2, 0);
    lv_obj_set_style_pad_all(draw_area_, 0, 0);
    lv_obj_set_scrollbar_mode(draw_area_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(draw_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(draw_area_, draw_cb, LV_EVENT_DRAW_POST, this);

    lv_obj_t* result = lv_label_create(screen_);
    const char* result_text;
    if (correct && guessed_early_) {
        result_text = "Correct! +2 (early)";
    } else if (correct) {
        result_text = "Correct! +1";
    } else {
        result_text = "Wrong!";
    }
    lv_label_set_text(result, result_text);
    lv_obj_set_style_text_color(result, correct ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(result, &lv_font_montserrat_16, 0);
    lv_obj_align(result, LV_ALIGN_BOTTOM_MID, 0, -72);

    lv_obj_t* word_lbl = lv_label_create(screen_);
    char wbuf[40];
    snprintf(wbuf, sizeof(wbuf), "The word was: %s", words_[word_idx_]);
    lv_label_set_text(word_lbl, wbuf);
    lv_obj_set_style_text_color(word_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(word_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(word_lbl, LV_ALIGN_BOTTOM_MID, 0, -52);

    lv_obj_t* score_lbl = lv_label_create(screen_);
    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "Score - P1: %d  P2: %d", score_[0], score_[1]);
    lv_label_set_text(score_lbl, sbuf);
    lv_obj_set_style_text_color(score_lbl, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(score_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(score_lbl, LV_ALIGN_BOTTOM_MID, 0, -34);

    bool last_round = (round_ + 1 >= TOTAL_ROUNDS);
    // In network mode, only host advances rounds
    bool show_next = !network_mode_ || is_host_;
    if (show_next) {
        lv_obj_t* btn = ui_create_btn(screen_, last_round ? "See Results" : "Next Round", 160, 30);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            Pictionary* self = (Pictionary*)lv_event_get_user_data(e);
            self->round_++;
            self->drawer_ ^= 1;
            if (self->network_mode_) {
                // Send next/over to peer
                if (self->round_ >= TOTAL_ROUNDS) {
                    StaticJsonDocument<96> doc;
                    doc["type"] = "move"; doc["game"] = "pictionary";
                    doc["a"] = "over";
                    doc["s0"] = self->score_[0];
                    doc["s1"] = self->score_[1];
                    char buf[96];
                    serializeJson(doc, buf, sizeof(buf));
                    self->send_json(buf);
                    self->show_gameover();
                } else {
                    StaticJsonDocument<128> doc;
                    doc["type"] = "move"; doc["game"] = "pictionary";
                    doc["a"] = "next";
                    doc["r"] = self->round_;
                    doc["d"] = self->drawer_;
                    doc["s0"] = self->score_[0];
                    doc["s1"] = self->score_[1];
                    char buf[128];
                    serializeJson(doc, buf, sizeof(buf));
                    self->send_json(buf);
                    self->start_round();
                }
            } else {
                if (self->round_ >= TOTAL_ROUNDS) {
                    self->show_gameover();
                } else {
                    self->start_round();
                }
            }
        }, LV_EVENT_CLICKED, this);
    } else {
        lv_obj_t* wait = lv_label_create(screen_);
        lv_label_set_text(wait, "Waiting for host...");
        lv_obj_set_style_text_color(wait, UI_COLOR_DIM, 0);
        lv_obj_set_style_text_font(wait, &lv_font_montserrat_12, 0);
        lv_obj_align(wait, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

void Pictionary::show_gameover() {
    clear_ui();
    phase_ = PHASE_GAMEOVER;

    lv_obj_t* title = ui_create_title(screen_, "Game Over!");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t* winner = lv_label_create(screen_);
    if (score_[0] > score_[1]) {
        lv_label_set_text(winner, "Player 1 Wins!");
    } else if (score_[1] > score_[0]) {
        lv_label_set_text(winner, "Player 2 Wins!");
    } else {
        lv_label_set_text(winner, "It's a Tie!");
    }
    lv_obj_set_style_text_color(winner, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(winner, &lv_font_montserrat_20, 0);
    lv_obj_align(winner, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t* score_lbl = lv_label_create(screen_);
    char sbuf[40];
    snprintf(sbuf, sizeof(sbuf), "Player 1: %d    Player 2: %d", score_[0], score_[1]);
    lv_label_set_text(score_lbl, sbuf);
    lv_obj_set_style_text_color(score_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(score_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(score_lbl, LV_ALIGN_CENTER, 0, 15);

    lv_obj_t* again = ui_create_btn(screen_, "Play Again", 130, 36);
    lv_obj_align(again, LV_ALIGN_BOTTOM_MID, -72, -20);
    lv_obj_add_event_cb(again, play_again_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* menu = ui_create_btn(screen_, "Menu", 100, 36);
    lv_obj_align(menu, LV_ALIGN_BOTTOM_MID, 72, -20);
    lv_obj_add_event_cb(menu, back_cb, LV_EVENT_CLICKED, NULL);
}
