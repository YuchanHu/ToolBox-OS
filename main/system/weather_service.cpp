#include "weather_service.h"
#include "weather_decode.h"
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>

#include <algorithm>
#include <atomic>
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
        snprintf(s, sizeof(s), "%.8g", v->valuedouble);
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
WeatherCity query_city(const std::string &value) {
    WeatherCity city;
    auto colon = value.find(':');
    if (colon != std::string::npos) {
        city.mode = value.substr(0, colon);
        city.input = value.substr(colon + 1);
    }
    return city;
}
std::string query_key(const WeatherCity &city) {
    return city.mode + ":" + city.input;
}
std::string compact_cache(cJSON *response, const WeatherCity &city) {
    // The offline snapshot keeps daily forecasts and a compact subset of hourly fields.
    auto *hours = cJSON_AddArrayToObject(response, "hours");
    cJSON *h;
    cJSON_ArrayForEach(h, field(response, "hourly_forecast")) {
        if (cJSON_GetArraySize(hours) >= 24) break;
        auto *row = cJSON_CreateArray();
        for (auto key : {"time", "temperature", "pop", "precip"})
            cJSON_AddItemToArray(row, cJSON_CreateString(str(h, key).c_str()));
        cJSON_AddItemToArray(hours, row);
    }
    cJSON_DeleteItemFromObject(response, "hourly_forecast");
    cJSON_AddStringToObject(response, "query", query_key(city).c_str());
    cJSON_AddNumberToObject(response, "saved", std::time(nullptr));
    char *serialized = cJSON_PrintUnformatted(response);
    std::string cache = serialized ? serialized : "";
    cJSON_free(serialized);
    if (cache.size() > 3500) {
        cJSON_DeleteItemFromObject(response, "hours");
        serialized = cJSON_PrintUnformatted(response);
        cache = serialized ? serialized : "";
        cJSON_free(serialized);
    }
    return cache.size() <= 3500 ? cache : std::string{};
}
std::atomic<bool> transport_busy{false};
} // namespace

struct WeatherService::Request {
    std::string query;
    WeatherCity city;
    WeatherResult next;
    std::atomic<bool> cancelled{false}, finished{false};
    bool started = false; // Foreground only.
    std::string cache;
    void run();
    std::string get(const std::string& url);
};

WeatherService::WeatherService() {
    Settings settings("weather");
    const auto key = settings.GetString("query");
    const auto legacy = parse(settings.GetString("city"));
    const auto old_id = str(legacy.get(), "id");
    result_.city = query_city(key.empty() && !old_id.empty() ? "adcode:" + old_id : key);
    result_.city.name = result_.city.input;
    auto root = parse(settings.GetString("cache"));
    if (field(root.get(), "query")) {
        auto *hours = cJSON_AddArrayToObject(root.get(), "hourly_forecast");
        cJSON *row;
        cJSON_ArrayForEach(row, field(root.get(), "hours")) {
            auto *hour = cJSON_CreateObject();
            int i = 0;
            for (auto k : {"time", "temperature", "pop", "precip"}) {
                auto *value = cJSON_GetArrayItem(row, i++);
                if (cJSON_IsString(value)) cJSON_AddStringToObject(hour, k, value->valuestring);
            }
            cJSON_AddItemToArray(hours, hour);
        }
        const auto *saved = field(root.get(), "saved");
        if (decode_weather(result_, root.get()) && cJSON_IsNumber(saved) &&
            str(root.get(), "query") == query_key(result_.city))
            result_.fetched_at = static_cast<int64_t>(saved->valuedouble);
    } else if (key.empty() && !old_id.empty() && str(field(root.get(), "city"), "id") == old_id) {
        auto data = std::make_shared<Forecast>();
        cJSON *p;
        cJSON_ArrayForEach(p, field(root.get(), "days")) {
            if (!cJSON_IsTrue(field(p, "valid"))) continue;
            auto &d = data->days.emplace_back();
            d.date = str(p, "date");
            d.updated = str(p, "updated");
            d.temperature = str(p, "temp");
            d.high = str(p, "high");
            d.low = str(p, "low");
            d.condition = str(p, "condition");
            d.code = atoi(str(p, "code").c_str());
            d.valid = true;
        }
        result_.city.name = str(legacy.get(), "name");
        result_.forecast = data;
    }
    result_.cached = true;
    result_.message = "离线缓存";
}
WeatherService::~WeatherService() {
    if (request_) request_->cancelled.store(true, std::memory_order_release);
}

WeatherResult WeatherService::snapshot() {
    start_pending();
    if (request_ && request_->finished.load(std::memory_order_acquire)) {
        auto request = std::move(request_);
        if (!request->cache.empty()) {
            Settings settings("weather", true);
            auto status = settings.TrySetString("cache", request->cache);
            if (status != ESP_OK) ESP_LOGW("Weather", "Cache write: %s", esp_err_to_name(status));
        }
        request->next.busy = false;
        request->next.revision = result_.revision + 1;
        result_ = std::move(request->next);
    }
    return result_;
}

std::string WeatherService::Request::get(const std::string &url) {
    if (cancelled)
        return {};
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.timeout_ms = 8000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.buffer_size = 2048;
    config.disable_auto_redirect = true;
    struct Response {
        const std::atomic<bool>& cancelled;
        std::string body;
        bool overflow = false;
    } response{cancelled, {}};
    config.is_async = true;
    config.user_data = &response;
    config.event_handler = [](esp_http_client_event_t *event) -> esp_err_t {
        auto *response = static_cast<Response *>(event->user_data);
        if (event->event_id == HTTP_EVENT_ON_DATA && !response->cancelled) {
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
        if (!cancelled) vTaskDelay(pdMS_TO_TICKS(20));
    } while (!cancelled && !response.overflow && esp_timer_get_time() < deadline);
    const bool success = status == ESP_OK && esp_http_client_get_status_code(client) == 200 && !response.overflow;
    ESP_LOGI("Weather", "UAPI direct HTTPS result=%s, HTTP=%d, bytes=%u", esp_err_to_name(status),
             esp_http_client_get_status_code(client), static_cast<unsigned>(response.body.size()));
    // The worker owns the HTTP handle through DNS, TLS and cleanup.
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return cancelled || !success ? std::string{} : response.body;
}
void WeatherService::search(const std::string &query) {
    launch(query, {});
}
void WeatherService::refresh(const WeatherCity &city) {
    if (request_) return;
    if (query_key(result_.city) != query_key(city)) {
        Settings settings("weather", true);
        if (settings.TrySetString("query", query_key(city)) != ESP_OK) {
            result_.message = "城市保存失败";
            ++result_.revision;
            return;
        }
        auto cached_name = result_.city.name;
        result_.city = city;
        if (result_.forecast) result_.city.name = cached_name;
        result_.cached = true;
        result_.fetched_at = 0;
    }
    launch({}, city);
}

void WeatherService::launch(const std::string &query, const WeatherCity &city) {
    if (request_) return;
    if (!GetHAL().getWifiStatus().connected) {
        result_.message = "离线缓存";
        ++result_.revision;
        return;
    }
    result_.busy = true;
    result_.message = "正在更新";
    result_.cities.clear();
    ++result_.revision;
    request_ = std::make_shared<Request>();
    request_->query = query;
    request_->city = city;
    request_->next = result_;
    start_pending();
}

void WeatherService::start_pending() {
    if (!request_ || request_->started || transport_busy.exchange(true)) return;
    auto* payload = new std::shared_ptr<Request>(request_);
    const auto created = xTaskCreate([](void* argument) {
        {
            std::unique_ptr<std::shared_ptr<Request>> job(static_cast<std::shared_ptr<Request>*>(argument));
            (*job)->run();
        }
        transport_busy.store(false);
        vTaskDelete(nullptr);
    }, "weather", 12288, payload, 2, nullptr);
    if (created != pdPASS) {
        delete payload;
        request_.reset();
        transport_busy.store(false);
        result_.busy = false;
        result_.message = "请求失败，显示缓存";
        ++result_.revision;
        return;
    }
    request_->started = true;
}

void WeatherService::Request::run() {
    WeatherCity selected = city;
    if (!query.empty()) {
        selected.mode = query.size() == 6 && query.find_first_not_of("0123456789") == std::string::npos ? "adcode" : "city";
        selected.input = query;
    }
    std::string url = "https://uapis.cn/api/v1/misc/weather?lang=zh";
    if (selected.mode != "auto") url += "&" + selected.mode + "=" + encode(selected.input);
    if (query.empty()) url += "&extended=true&forecast=true&hourly=true";
    auto response = parse(get(url));
    if (cancelled) return;
    if (!query.empty()) {
        selected.name = str(response.get(), "city");
        if (!selected.name.empty() && !str(response.get(), "temperature").empty()) next.cities = {selected};
        next.message = next.cities.empty() ? "未找到城市或网络不可用" : "选择城市";
    } else if (decode_weather(next, response.get())) {
        next.cached = false;
        next.fetched_at = std::time(nullptr);
        next.message = "更新完成";
        cache = compact_cache(response.get(), selected);
    } else {
        next.message = next.forecast ? "请求失败，显示缓存" : "更新失败，下拉重试";
        next.cached = true;
    }
    finished.store(true, std::memory_order_release);
}
} // namespace toolbox
