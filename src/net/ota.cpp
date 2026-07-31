#include "ota.h"
#include "config.h"
#include "../utils/crash_trace.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

static WebServer server(OTA_PORT);
static bool ota_update_done = false;

static const char* reset_reason_str() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Power-on";
        case ESP_RST_EXT:       return "External reset";
        case ESP_RST_SW:        return "Software reset (ESP.restart)";
        case ESP_RST_PANIC:     return "PANIC (crash)";
        case ESP_RST_INT_WDT:   return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "Task watchdog";
        case ESP_RST_WDT:       return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        case ESP_RST_BROWNOUT:  return "Brownout (low power)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Unknown";
    }
}

void ota_validate_app() {
    esp_ota_mark_app_valid_cancel_rollback();

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot    = esp_ota_get_boot_partition();
    Serial.printf("[OTA] Running: %s (0x%06x)  Boot: %s (0x%06x)\n",
                  running ? running->label : "?", running ? running->address : 0,
                  boot ? boot->label : "?", boot ? boot->address : 0);
}

void ota_init() {
    if (WiFi.status() != WL_CONNECTED) return;

    // Custom /update page — registered BEFORE ElegantOTA to override default
    server.on("/update", HTTP_GET, []() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char hw_id[18];
        snprintf(hw_id, sizeof(hw_id), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        String page = "<!DOCTYPE html><html><head><title>CYD Arcade OTA</title>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>"
            "body{font-family:-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:20px;background:#1a1a2e;color:#e0e0e0}"
            "h1{color:#e94560;text-align:center;margin-bottom:5px}"
            ".ver{text-align:center;color:#888;margin-bottom:20px}"
            "table{width:100%;border-collapse:collapse;margin-bottom:20px}"
            "td{padding:10px 8px;border-bottom:1px solid #333}"
            "td:first-child{color:#888;width:40%}"
            ".upload{background:#16213e;border:2px dashed #e94560;border-radius:12px;padding:30px;text-align:center;margin-top:20px}"
            "input[type=file]{margin:15px 0}"
            "button{background:#e94560;color:#fff;border:none;padding:12px 32px;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer}"
            "button:hover{background:#c73650}"
            ".bar{width:100%;height:20px;background:#333;border-radius:10px;margin-top:15px;display:none}"
            ".bar div{height:100%;background:#4ecca3;border-radius:10px;width:0%;transition:width 0.3s}"
            ".msg{text-align:center;margin-top:10px;color:#4ecca3;display:none}"
            "</style></head><body>"
            "<h1>CYD Arcade</h1>"
            "<div class='ver'>v" FW_VERSION "</div>"
            "<table>"
            "<tr><td>Hardware</td><td>ESP32-2432S028 (CYD)</td></tr>"
            "<tr><td>HW ID</td><td>" + String(hw_id) + "</td></tr>"
            "<tr><td>Firmware</td><td>v" FW_VERSION "</td></tr>"
            "<tr><td>Partition</td><td>" + String(running ? running->label : "?") + "</td></tr>"
            "<tr><td>IP Address</td><td>" + WiFi.localIP().toString() + "</td></tr>"
            "<tr><td>SSID</td><td>" + WiFi.SSID() + "</td></tr>"
            "<tr><td>RSSI</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>"
            "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap() / 1024) + " KB</td></tr>"
            "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " s</td></tr>"
            "</table>"
            "<div class='upload'>"
            "<div>Select firmware .bin file</div>"
            "<form id='f'>"
            "<input type='file' id='fw' accept='.bin'><br>"
            "<button type='submit'>Upload Firmware</button>"
            "</form>"
            "<div class='bar' id='bar'><div id='p'></div></div>"
            "<div class='msg' id='msg'></div>"
            "</div>"
            "<script>"
            "document.getElementById('f').onsubmit=async function(e){"
            "e.preventDefault();"
            "var f=document.getElementById('fw').files[0];"
            "if(!f)return;"
            "await fetch('/ota/start?mode=firmware');"
            "var d=new FormData();d.append('firmware',f);"
            "var x=new XMLHttpRequest();"
            "document.getElementById('bar').style.display='block';"
            "x.upload.onprogress=function(e){if(e.lengthComputable)document.getElementById('p').style.width=(e.loaded/e.total*100)+'%'};"
            "x.onload=function(){"
            "var m=document.getElementById('msg');"
            "m.style.display='block';"
            "if(x.status==200){m.textContent='Update successful! Rebooting...';m.style.color='#4ecca3'}else{m.textContent='Update failed!';m.style.color='#e94560'}};"
            "x.open('POST','/ota/upload');"
            "x.send(d);"
            "};"
            "</script></body></html>";
        server.send(200, "text/html", page);
    });

    // Plain-text remote diagnostics — fetch with: curl http://<ip>/debug
    server.on("/debug", HTTP_GET, []() {
        String body;
        body += "FW:          v" FW_VERSION "\n";
        body += "Reset reason: " + String(reset_reason_str()) + "\n";
        body += "Free heap:    " + String(ESP.getFreeHeap()) + " bytes\n";
        body += "Min free heap:" + String(ESP.getMinFreeHeap()) + " bytes (lowest since boot)\n";
        body += "Largest block:" + String(ESP.getMaxAllocHeap()) + " bytes\n";
        body += "Uptime:       " + String(millis() / 1000) + " s\n";
        char trace[96];
        crash_trace_format(trace, sizeof(trace));
        body += "Crash trace:  " + String(trace) + "\n";
        server.send(200, "text/plain", body);
    });

    ElegantOTA.begin(&server);
    ElegantOTA.onEnd([](bool success) {
        if (success) {
            Serial.println("[OTA] Update complete — rebooting...");
            ota_update_done = true;
        } else {
            Serial.println("[OTA] Update failed.");
        }
    });

    server.begin();
    Serial.printf("[OTA] Ready at http://%s/update\n",
                  WiFi.localIP().toString().c_str());
}

void ota_loop() {
    if (WiFi.status() != WL_CONNECTED) return;
    server.handleClient();
    ElegantOTA.loop();

    if (ota_update_done) {
        ota_update_done = false;
        delay(500);
        ESP.restart();
    }
}
