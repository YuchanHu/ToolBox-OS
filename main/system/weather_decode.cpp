#include "weather_decode.h"
#include <cJSON.h>
#include <cstdio>
#include <cstdlib>
namespace toolbox {
namespace {
cJSON *field(cJSON *p, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(p, key);
}
std::string str(cJSON *p, const char *key) {
    auto *v = field(p, key);
    if (cJSON_IsString(v))
        return v->valuestring;
    if (cJSON_IsNumber(v)) {
        char text[32];
        snprintf(text, sizeof(text), "%.0f", v->valuedouble);
        return text;
    }
    return {};
}
} // namespace
bool decode_weather(WeatherResult &next, cJSON *root, int minutes) {
    auto weather_code = [](const std::string &text) {
        if (text.find("雷") != std::string::npos)
            return text.find("雹") != std::string::npos ? 304 : 302;
        if (text.find("夹雪") != std::string::npos)
            return 404;
        if (text.find("雪") != std::string::npos)
            return 400;
        if (text.find("冻雨") != std::string::npos)
            return 313;
        if (text.find("大雨") != std::string::npos || text.find("暴雨") != std::string::npos)
            return 307;
        if (text.find("雨") != std::string::npos)
            return 305;
        if (text.find("雾") != std::string::npos || text.find("霾") != std::string::npos)
            return 501;
        if (text.find("阴") != std::string::npos)
            return 104;
        if (text.find("云") != std::string::npos)
            return 103;
        if (text.find("晴") != std::string::npos)
            return 100;
        return 999;
    };
    bool success = false;
    auto *forecast = field(root, "forecast");
    for (int i = 0; i < cJSON_GetArraySize(forecast); ++i) {
        auto *p = cJSON_GetArrayItem(forecast, i);
        for (int j = 0; j < 2; ++j)
            if (str(p, "date") == next.days[j].date) {
                auto &d = next.days[j];
                d.high = str(p, "temp_max");
                d.low = str(p, "temp_min");
                d.condition = str(p, "weather_day");
                d.code = weather_code(d.condition);
                d.probability = str(p, "precipitation_probability");
                d.valid = !d.high.empty();
                d.updated = str(root, "report_time");
                success |= d.valid;
            }
    }
    if (cJSON_IsNumber(field(root, "temperature"))) {
        auto &d = next.days[0];
        d.temperature = str(root, "temperature");
        d.humidity = str(root, "humidity");
        d.wind = str(root, "wind_speed");
        d.condition = str(root, "weather");
        d.code = weather_code(d.condition);
        d.updated = str(root, "report_time");
        d.valid = true;
        d.night = minutes < 420 || minutes >= 1140;
        if (auto *today = cJSON_GetArrayItem(forecast, 0)) {
            auto minutes_of = [](const std::string &value) {
                return value.size() >= 5 ? atoi(value.substr(0, 2).c_str()) * 60 + atoi(value.substr(3, 2).c_str())
                                         : -1;
            };
            const int rise = minutes_of(str(today, "sunrise")), set = minutes_of(str(today, "sunset"));
            if (rise >= 0 && set >= 0)
                d.night = minutes < rise || minutes >= set;
        }
        success = true;
    }
    return success;
}
} // namespace toolbox
