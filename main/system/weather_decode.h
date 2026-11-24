#pragma once
#include "weather_service.h"
struct cJSON;
namespace toolbox {
bool decode_weather(WeatherResult& result,cJSON* root);
}
