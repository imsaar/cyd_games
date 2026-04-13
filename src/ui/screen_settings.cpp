#include "screen_settings.h"
#include "ui_common.h"
#include "screen_manager.h"
#include <WiFi.h>
#include <Arduino.h>
#include <esp_ota_ops.h>

static lv_obj_t* lbl_info = nullptr;

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

lv_obj_t* screen_settings_create() {
    lv_obj_t* scr = ui_create_screen();

    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lbl_info = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_info, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 15, 40);
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
