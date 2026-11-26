#include "weather_service.h"
#include "weather_decode.h"
#include <cJSON.h>
#include <esp_crc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <hal/utils/wear_levelling/wear_levelling.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include <memory>
#include <unistd.h>

namespace toolbox {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
constexpr size_t cache_limit = 3500;
Json parse(const std::string &s) {
    return Json(cJSON_ParseWithOpts(s.c_str(), nullptr, true), cJSON_Delete);
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
std::string compact_cache(cJSON* response, const WeatherResult& weather, const WeatherCity& city)
{
    Json cache(cJSON_CreateObject(), cJSON_Delete);
    for (auto key : {"city", "report_time", "temperature", "weather", "weather_icon", "temp_min", "temp_max",
                     "humidity", "wind_direction", "wind_power", "wind_speed", "precipitation", "uv",
                     "feels_like", "visibility", "pressure", "cloud", "aqi", "aqi_category", "air_pollutants", "alerts"}) {
        if (auto* value = field(response, key)) cJSON_AddItemToObject(cache.get(), key, cJSON_Duplicate(value, true));
    }
    auto* days = cJSON_AddArrayToObject(cache.get(), "forecast");
    const auto today = weather.forecast->days.front().date;
    for (const auto& d : weather.forecast->days) {
        if (d.high.empty() || d.low.empty()) continue;
        auto* row = cJSON_CreateObject();
        cJSON_AddStringToObject(row, "date", d.date.c_str());
        cJSON_AddStringToObject(row, "temp_min", d.low.c_str());
        cJSON_AddStringToObject(row, "temp_max", d.high.c_str());
        auto add = [&](const char* key, const std::string& value) {
            if (!value.empty()) cJSON_AddStringToObject(row, key, value.c_str());
        };
        add("week", d.week);
        add("weather_day", d.day_condition);
        add("weather_night", d.night_condition);
        add("sunrise", d.sunrise);
        add("sunset", d.sunset);
        if (d.date != today) {
            add("humidity", d.humidity);
            add("wind_dir_day", d.direction);
            add("wind_scale_day", d.power);
            add("wind_speed_day", d.wind);
            add("precip", d.precipitation);
            add("uv_index", d.uv);
        }
        cJSON_AddItemToArray(days, row);
    }
    auto* hours = cJSON_AddArrayToObject(cache.get(), "hours");
    for (const auto& h : weather.forecast->hours) {
        auto *row = cJSON_CreateArray();
        for (const auto* value : {&h.time, &h.temperature, &h.pop, &h.precip})
            cJSON_AddItemToArray(row, cJSON_CreateString(value->c_str()));
        cJSON_AddItemToArray(hours, row);
    }
    cJSON_AddStringToObject(cache.get(), "query", query_key(city).c_str());
    cJSON_AddNumberToObject(cache.get(), "saved", weather.fetched_at);
    auto serialize = [&] {
        std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(cache.get()), cJSON_free);
        return json ? std::string(json.get()) : std::string{};
    };
    auto data = serialize();
    if (data.size() > cache_limit) {
        cJSON_DeleteItemFromObject(cache.get(), "hours");
        data = serialize();
    }
    return data.size() <= cache_limit ? std::move(data) : std::string{};
}

std::string cache_path(int slot)
{
    return std::string(wear_levelling_get_base_path()) + (slot == 0 ? "/weather.0" : "/weather.1");
}

struct CacheHeader {
    uint32_t sequence, size, crc;
};

uint32_t cache_crc(const std::string& data)
{
    return esp_crc32_le(0, reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string read_cache(int slot, uint32_t& sequence)
{
    const auto file = std::unique_ptr<FILE, decltype(&fclose)>(fopen(cache_path(slot).c_str(), "rb"), fclose);
    CacheHeader header;
    if (!file || fread(&header, sizeof(header), 1, file.get()) != 1 || !header.sequence ||
        !header.size || header.size > cache_limit) return {};
    std::string data(header.size, '\0');
    if (fread(data.data(), 1, data.size(), file.get()) != data.size() || cache_crc(data) != header.crc) return {};
    sequence = header.sequence;
    return data;
}

bool write_cache(int slot, uint32_t sequence, const std::string& data)
{
    auto* file = fopen(cache_path(slot).c_str(), "wb");
    if (!file) return false;
    const CacheHeader header{sequence, static_cast<uint32_t>(data.size()), cache_crc(data)};
    const bool written = fwrite(&header, sizeof(header), 1, file) == 1 &&
                         fwrite(data.data(), 1, data.size(), file) == data.size() &&
                         fflush(file) == 0 && fsync(fileno(file)) == 0;
    return fclose(file) == 0 && written;
}

bool newer_cache(uint32_t sequence, uint32_t previous)
{
    return !previous || static_cast<int32_t>(sequence - previous) > 0;
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
    const auto legacy = parse(key.empty() ? settings.GetString("city") : std::string{});
    const auto old_id = str(legacy.get(), "id");
    result_.city = query_city(key.empty() && !old_id.empty() ? "adcode:" + old_id : key);
    result_.city.name = result_.city.input;
    const auto query = query_key(result_.city);
    uint32_t loaded = 0;
    for (int slot = 0; slot < 2; ++slot) {
        uint32_t sequence = 0;
        auto cache = parse(read_cache(slot, sequence));
        if (!cache) continue;
        if (newer_cache(sequence, cache_sequence_)) {
            cache_sequence_ = sequence;
            cache_slot_ = slot;
        }
        if (newer_cache(sequence, loaded) && decode_cached_weather(result_, cache.get(), query)) loaded = sequence;
    }
    const auto legacy_cache = settings.GetString("cache");
    cache_in_nvs_ = !legacy_cache.empty();
    auto root = parse(legacy_cache);
    auto fallback = result_;
    if (decode_cached_weather(fallback, root.get(), query) && (!loaded || fallback.fetched_at > result_.fetched_at)) {
        result_ = std::move(fallback);
    } else if (!result_.forecast && key.empty() && !old_id.empty() &&
               str(field(root.get(), "city"), "id") == old_id) {
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

void WeatherService::save_cache(const std::string& data)
{
    const int slot = cache_slot_ == 0 ? 1 : 0;
    uint32_t sequence = cache_sequence_ + 1;
    if (!sequence) sequence = 1;
    // An incomplete write can only invalidate the older slot. Recover the
    // other slot by its CRC and sequence on the next start.
    if (write_cache(slot, sequence, data)) {
        cache_slot_ = slot;
        cache_sequence_ = sequence;
        if (cache_in_nvs_) {
            Settings settings("weather", true);
            cache_in_nvs_ = settings.TryEraseKey("cache") != ESP_OK || settings.Commit() != ESP_OK;
        }
        return;
    }
    Settings settings("weather", true);
    auto status = settings.TrySetString("cache", data);
    if (status == ESP_OK) status = settings.Commit();
    if (status == ESP_OK) cache_in_nvs_ = true;
    else ESP_LOGW("Weather", "Cache write: %s", esp_err_to_name(status));
}

WeatherService::~WeatherService() {
    if (request_) request_->cancelled.store(true, std::memory_order_release);
}

WeatherResult WeatherService::snapshot() {
    start_pending();
    if (request_ && request_->finished.load(std::memory_order_acquire)) {
        auto request = std::move(request_);
        if (!request->cache.empty()) save_cache(request->cache);
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
        result_.city = city;
        if (result_.city.name.empty()) result_.city.name = result_.city.input;
        result_.forecast.reset();
        result_.cached = true;
        result_.fetched_at = 0;
        ++result_.revision;
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
        cache = compact_cache(response.get(), next, selected);
    } else {
        next.message = next.forecast ? "请求失败，显示缓存" : "更新失败，下拉重试";
        next.cached = true;
    }
    finished.store(true, std::memory_order_release);
}
} // namespace toolbox
