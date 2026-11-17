#include "weather_service.h"
#include "weather_decode.h"
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_pthread.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include <memory>

namespace toolbox {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json parse(const std::string &s) {
    return Json(cJSON_Parse(s.c_str()), cJSON_Delete);
}
cJSON *field(cJSON *p, const char *k) {
    return cJSON_GetObjectItemCaseSensitive(p, k);
}
std::string str(cJSON *p, const char *k) {
    auto *v = field(p, k);
    if (cJSON_IsString(v))
        return v->valuestring;
    if (cJSON_IsNumber(v)) {
        char s[32];
        snprintf(s, sizeof(s), "%.0f", v->valuedouble);
        return s;
    }
    return {};
}
std::string encode(const std::string &value) {
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
            c == '_')
            result += c;
        else {
            char s[4];
            snprintf(s, sizeof(s), "%%%02X", c);
            result += s;
        }
    }
    return result;
}
std::string date(int offset, int day = 0, bool compact = false) {
    time_t time = std::time(nullptr) + offset + day * 86400;
    std::tm tm = {};
    gmtime_r(&time, &tm);
    char s[24];
    strftime(s, sizeof(s), compact ? "%Y%m%d" : "%Y-%m-%d", &tm);
    return s;
}
void put(cJSON *obj, const char *key, const std::string &value) {
    cJSON_AddStringToObject(obj, key, value.c_str());
}
std::string json_string(cJSON *p) {
    char *value = cJSON_PrintUnformatted(p);
    std::string result = value ? value : "";
    cJSON_free(value);
    return result;
}
cJSON *city_json(const WeatherCity &c) {
    auto *p = cJSON_CreateObject();
    put(p, "id", c.id);
    put(p, "name", c.name);
    put(p, "lat", c.latitude);
    put(p, "lon", c.longitude);
    put(p, "tz", c.timezone);
    put(p, "province", c.province);
    cJSON_AddNumberToObject(p, "offset", c.offset);
    return p;
}
WeatherCity read_city(cJSON *p) {
    WeatherCity c;
    c.id = str(p, "id");
    c.name = str(p, "name");
    c.latitude = str(p, "lat");
    c.longitude = str(p, "lon");
    c.timezone = str(p, "tz");
    c.province = str(p, "province");
    auto *offset = field(p, "offset");
    if (cJSON_IsNumber(offset))
        c.offset = offset->valueint;
    return c;
}
void save_cache(const WeatherResult &result) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddItemToObject(root.get(), "city", city_json(result.city));
    auto *days = cJSON_AddArrayToObject(root.get(), "days");
    for (const auto &d : result.days) {
        auto *p = cJSON_CreateObject();
        put(p, "date", d.date);
        put(p, "temp", d.temperature);
        put(p, "high", d.high);
        put(p, "low", d.low);
        put(p, "condition", d.condition);
        put(p, "humidity", d.humidity);
        put(p, "wind", d.wind);
        put(p, "precip", d.precipitation);
        put(p, "prob", d.probability);
        put(p, "updated", d.updated);
        cJSON_AddNumberToObject(p, "code", d.code);
        cJSON_AddBoolToObject(p, "valid", d.valid);
        cJSON_AddBoolToObject(p, "night", d.night);
        cJSON_AddItemToArray(days, p);
    }
    Settings settings("weather", true);
    settings.SetString("cache", json_string(root.get()));
}
} // namespace
WeatherService::WeatherService() {
    Settings settings("weather");
    auto root = parse(settings.GetString("cache"));
    result_.city = read_city(field(root.get(), "city"));
    auto *days = field(root.get(), "days");
    for (int i = 0; i < cJSON_GetArraySize(days); ++i) {
        auto *p = cJSON_GetArrayItem(days, i);
        const auto stored = str(p, "date");
        for (int j = 0; j < 2; ++j)
            if (stored == date(result_.city.offset, j)) {
                auto &d = result_.days[j];
                d.date = stored;
                d.temperature = str(p, "temp");
                d.high = str(p, "high");
                d.low = str(p, "low");
                d.condition = str(p, "condition");
                d.humidity = str(p, "humidity");
                d.wind = str(p, "wind");
                d.precipitation = str(p, "precip");
                d.probability = str(p, "prob");
                d.updated = str(p, "updated");
                d.code = atoi(str(p, "code").c_str());
                d.valid = cJSON_IsTrue(field(p, "valid"));
                d.night = cJSON_IsTrue(field(p, "night"));
            }
    }
    auto selected = parse(settings.GetString("city"));
    const auto city = read_city(selected.get());
    if (!city.id.empty() && city.id != result_.city.id) {
        result_.city = city;
        result_.days = {};
    }
    result_.cached = true;
    result_.message = "选择城市";
}
WeatherService::~WeatherService() {
    stop();
}
void WeatherService::stop() {
    cancelled_ = true;
    if (worker_.joinable())
        worker_.join();
}
WeatherResult WeatherService::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}
std::string WeatherService::request(const std::string &url) {
    if (cancelled_)
        return {};
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 8000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 2048;
    config.disable_auto_redirect = true;
    struct Response {
        std::string body;
        bool overflow = false;
    } response;
    config.is_async = true;
    config.user_data = &response;
    config.event_handler = [](esp_http_client_event_t *event) -> esp_err_t {
        auto *response = static_cast<Response *>(event->user_data);
        if (event->event_id == HTTP_EVENT_ON_DATA) {
            if (response->body.size() + event->data_len > 65536) {
                response->overflow = true;
                return ESP_FAIL;
            }
            response->body.append(static_cast<const char *>(event->data), event->data_len);
        }
        return ESP_OK;
    };
    auto client = esp_http_client_init(&config);
    if (!client)
        return {};
    const int64_t deadline = esp_timer_get_time() + 12000000;
    esp_err_t status;
    do {
        status = esp_http_client_perform(client);
        if (status != ESP_ERR_HTTP_EAGAIN)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (!cancelled_ && !response.overflow && esp_timer_get_time() < deadline);
    const bool success = status == ESP_OK && esp_http_client_get_status_code(client) == 200 && !response.overflow;
    ESP_LOGI("Weather", "UAPI direct HTTPS result=%s, HTTP=%d, bytes=%u", esp_err_to_name(status),
             esp_http_client_get_status_code(client), static_cast<unsigned>(response.body.size()));
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return cancelled_ || !success ? std::string{} : response.body;
}
void WeatherService::search(const std::string &query) {
    launch(query, {});
}
void WeatherService::refresh(const WeatherCity &city) {
    if (!finished_)
        return;
    {
        Settings settings("weather", true);
        Json p(city_json(city), cJSON_Delete);
        settings.SetString("city", json_string(p.get()));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (result_.city.id != city.id)
            result_.days = {};
        result_.city = city;
    }
    launch({}, city);
}
void WeatherService::launch(const std::string &query, const WeatherCity &city) {
    if (!finished_)
        return;
    if (worker_.joinable())
        worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!GetHAL().getWifiStatus().connected) {
            result_.message = "离线缓存";
            ++result_.revision;
            return;
        }
        result_.busy = true;
        result_.message = "正在更新";
        result_.cities.clear();
        ++result_.revision;
    }
    cancelled_ = false;
    finished_ = false;
    auto config = esp_pthread_get_default_config();
    config.stack_size = 12288;
    config.prio = 2;
    config.thread_name = "weather";
    esp_pthread_set_cfg(&config);
    worker_ = std::thread([this, query, city] {
        auto next = snapshot();
        auto domestic =
            parse(request("https://uapis.cn/api/v1/misc/weather?" + std::string(query.empty() ? "adcode=" : "city=") +
                          encode(query.empty() ? city.id : query) + "&lang=zh&extended=true&forecast=true"));
        if (!query.empty()) {
            if (cJSON_IsNumber(field(domestic.get(), "temperature")) && !str(domestic.get(), "adcode").empty()) {
                WeatherCity found;
                found.id = str(domestic.get(), "adcode");
                found.name = str(domestic.get(), "city");
                found.province = str(domestic.get(), "province");
                found.timezone = "Asia/Shanghai";
                next.cities = {found};
            }
            next.message = next.cities.empty() ? "未找到城市或网络不可用" : "选择城市";
        } else {
            if (next.city.id != city.id)
                next.days = {};
            next.city = city;
            for (int i = 0; i < 2; ++i) {
                const auto expected = date(28800, i);
                if (next.days[i].date != expected)
                    next.days[i] = {};
                next.days[i].date = expected;
            }
            time_t now = std::time(nullptr) + 28800;
            std::tm local = {};
            gmtime_r(&now, &local);
            const bool success = str(domestic.get(), "adcode") == city.id &&
                                 decode_weather(next, domestic.get(), local.tm_hour * 60 + local.tm_min);
            if (success && !str(domestic.get(), "city").empty()) next.city.name = str(domestic.get(), "city");
            next.message = success ? "更新完成" : "请求失败，显示缓存";
            next.cached = !success;
            if (!cancelled_ && success)
                save_cache(next);
        }
        if (!cancelled_) {
            std::lock_guard<std::mutex> lock(mutex_);
            next.busy = false;
            next.revision = result_.revision + 1;
            result_ = std::move(next);
        }
        finished_ = true;
    });
}
} // namespace toolbox
