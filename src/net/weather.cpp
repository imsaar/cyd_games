#include "weather.h"
#include "wifi_manager.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Seattle coordinates
static const char* WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=47.6062&longitude=-122.3321"
    "&current=temperature_2m,weathercode"
    "&daily=temperature_2m_max,temperature_2m_min,weathercode"
    "&temperature_unit=fahrenheit"
    "&timezone=America/Los_Angeles"
    "&forecast_days=7";

static WeatherData data_ = {};
static uint32_t last_fetch_ = 0;
static const uint32_t FETCH_INTERVAL = 15UL * 60 * 1000;  // 15 minutes
static bool fetching_ = false;

void weather_init() {
    data_.valid = false;
    last_fetch_ = 0;
}

static void do_fetch() {
    if (!wifi_connected()) return;
    fetching_ = true;

    HTTPClient http;
    http.begin(WEATHER_URL);
    http.setTimeout(8000);
    int code = http.GET();

    if (code == 200) {
        String payload = http.getString();
        // Use DynamicJsonDocument for large response
        DynamicJsonDocument doc(2048);
        if (!deserializeJson(doc, payload)) {
            data_.current_temp = doc["current"]["temperature_2m"] | 0.0f;
            data_.current_code = doc["current"]["weathercode"] | 0;

            JsonArray maxs = doc["daily"]["temperature_2m_max"];
            JsonArray mins = doc["daily"]["temperature_2m_min"];
            JsonArray codes = doc["daily"]["weathercode"];
            int days = maxs.size();
            if (days > 7) days = 7;
            data_.forecast_days = days;
            for (int i = 0; i < days; i++) {
                data_.forecast[i].temp_max = maxs[i] | 0.0f;
                data_.forecast[i].temp_min = mins[i] | 0.0f;
                data_.forecast[i].code = codes[i] | 0;
            }
            data_.valid = true;
        }
    }
    http.end();
    fetching_ = false;
}

void weather_update() {
    if (fetching_) return;
    uint32_t now = millis();
    if (last_fetch_ == 0 || (now - last_fetch_) > FETCH_INTERVAL) {
        last_fetch_ = now;
        do_fetch();
    }
}

const WeatherData* weather_get() { return &data_; }

const char* weather_code_str(int code) {
    if (code == 0) return "Clear";
    if (code <= 3) return "Cloudy";
    if (code <= 48) return "Fog";
    if (code <= 55) return "Drizzle";
    if (code <= 57) return "Frzg Drzl";
    if (code <= 65) return "Rain";
    if (code <= 67) return "Frzg Rain";
    if (code <= 75) return "Snow";
    if (code <= 77) return "Grains";
    if (code <= 82) return "Showers";
    if (code <= 86) return "Snow Shwr";
    if (code <= 99) return "T-Storm";
    return "?";
}

const char* weather_code_icon(int code) {
    if (code == 0) return "*";   // sun
    if (code <= 3) return "~";   // cloudy
    if (code <= 48) return "=";  // fog
    if (code <= 67) return ",";  // rain
    if (code <= 77) return ".";  // snow
    if (code <= 86) return ",";  // showers
    return "!";                  // thunder
}
