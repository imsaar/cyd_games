#pragma once
#include <stdint.h>

struct WeatherDay {
    float temp_max;
    float temp_min;
    int   code;
};

struct WeatherData {
    bool  valid;
    float current_temp;
    int   current_code;
    WeatherDay forecast[7];
    int   forecast_days;
};

void               weather_init();
void               weather_update();  // call periodically, fetches every 15 min
const WeatherData* weather_get();
const char*        weather_code_str(int code);
const char*        weather_code_icon(int code);
