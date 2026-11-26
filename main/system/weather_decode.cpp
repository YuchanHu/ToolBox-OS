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
        snprintf(text, sizeof(text), "%.8g", v->valuedouble);
        return text;
    }
    return {};
}
} // namespace
bool decode_weather(WeatherResult &next, cJSON *root) {
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
    const auto temperature = str(root, "temperature");
    const auto report = str(root, "report_time");
    if (temperature.empty() || report.size() < 16 || str(root, "city").empty()) return false;
    auto data = std::make_shared<Forecast>();
    const auto today = report.substr(0, 10);
    auto &days = data->days;
    days.emplace_back();
    days.front().date = today;
    cJSON *item;
    cJSON_ArrayForEach(item, field(root, "forecast")) {
        const auto date = str(item, "date");
        if (date.size() != 10 || date < today || str(item, "temp_min").empty() || str(item, "temp_max").empty()) continue;
        if (date > today && (days.size() == 7 || date <= days.back().date)) continue;
        auto &day = date == today ? days.front() : days.emplace_back();
        day.date = date;
        day.week = str(item, "week");
        day.high = str(item, "temp_max");
        day.low = str(item, "temp_min");
        day.day_condition = str(item, "weather_day");
        day.condition = day.day_condition;
        day.night_condition = str(item, "weather_night");
        day.direction = str(item, "wind_dir_day");
        day.power = str(item, "wind_scale_day");
        day.wind = str(item, "wind_speed_day");
        day.humidity = str(item, "humidity");
        day.precipitation = str(item, "precip");
        day.uv = str(item, "uv_index");
        day.sunrise = str(item, "sunrise");
        day.sunset = str(item, "sunset");
        day.code = weather_code(day.condition);
        day.valid = true;
    }
    auto &d = days.front();
    d.valid = true;
    d.updated = report;
    d.temperature = temperature;
    d.condition = str(root, "weather");
    if (d.high.empty()) d.high = str(root, "temp_max");
    if (d.low.empty()) d.low = str(root, "temp_min");
    auto icon = str(root, "weather_icon");
    d.code = icon.empty() ? weather_code(d.condition) : atoi(icon.c_str());
    d.humidity = str(root, "humidity");
    d.direction = str(root, "wind_direction");
    d.power = str(root, "wind_power");
    d.wind = str(root, "wind_speed");
    d.precipitation = str(root, "precipitation");
    d.uv = str(root, "uv");
    d.feels_like = str(root, "feels_like");
    d.visibility = str(root, "visibility");
    d.pressure = str(root, "pressure");
    d.cloud = str(root, "cloud");
    d.aqi = str(root, "aqi");
    d.category = str(root, "aqi_category");
    for (auto key : {"pm25", "pm10", "o3", "no2", "so2", "co"}) {
        auto value = str(field(root, "air_pollutants"), key);
        if (!value.empty()) d.pollutants += std::string(key) + " " + value + " ";
    }
    cJSON_ArrayForEach(item, field(root, "alerts")) {
        d.warning += str(item, "title") + " ";
    }
    const auto hhmm = report.substr(11, 5);
    d.night = hhmm < (d.sunrise.empty() ? "07:00" : d.sunrise) || hhmm >= (d.sunset.empty() ? "19:00" : d.sunset);
    auto local_minute = [](std::string stamp) {
        if (stamp.size() > 10) stamp[10] = ' ';
        return stamp.substr(0, 16);
    };
    const auto report_minute = local_minute(report);
    cJSON_ArrayForEach(item, field(root, "hourly_forecast")) {
        auto time = str(item, "time");
        if (time.size() < 16 || local_minute(time) < report_minute || str(item, "temperature").empty()) continue;
        data->hours.push_back({time, str(item, "temperature"), str(item, "weather"), str(item, "humidity"),
            str(item, "wind_direction"), str(item, "wind_speed"), str(item, "pop"), str(item, "precip"), str(item, "feels_like")});
        if (data->hours.size() == 24) break;
    }
    next.city.name = str(root, "city");
    next.forecast = std::move(data);
    return true;
}

bool decode_cached_weather(WeatherResult& result, cJSON* root, const std::string& query)
{
    const auto* saved = field(root, "saved");
    if (!cJSON_IsNumber(saved) || str(root, "query") != query) return false;

    if (const auto* compact = field(root, "hours"); cJSON_IsArray(compact)) {
        auto* hours = cJSON_AddArrayToObject(root, "hourly_forecast");
        const cJSON* row;
        cJSON_ArrayForEach(row, compact) {
            auto* hour = cJSON_CreateObject();
            int index = 0;
            for (auto key : {"time", "temperature", "pop", "precip"}) {
                const auto* value = cJSON_GetArrayItem(row, index++);
                if (cJSON_IsString(value)) cJSON_AddStringToObject(hour, key, value->valuestring);
            }
            cJSON_AddItemToArray(hours, hour);
        }
    }
    if (!decode_weather(result, root)) return false;
    result.fetched_at = static_cast<int64_t>(saved->valuedouble);
    return true;
}
} // namespace toolbox
