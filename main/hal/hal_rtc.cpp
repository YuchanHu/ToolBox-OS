/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include "drivers/rx8130/rx8130.h"
#include <mooncake_log.h>
#include <memory>
#include <sys/time.h>
#include <ctime>
#include <atomic>
#include <mutex>
#include <esp_netif_sntp.h>
#include <esp_log.h>

static const std::string_view _tag = "HAL-RTC";
static std::unique_ptr<RX8130_Class> _rx8130;
static std::mutex rtc_mutex;
static std::atomic<bool> rtc_write_pending{false};
static bool sntp_started = false;


namespace {

bool _get_local_time(struct tm& tm_curr)
{
    std::time_t now = std::time(nullptr);
    return localtime_r(&now, &tm_curr) != nullptr;
}

bool _apply_local_time(struct tm& tm_curr)
{
    const std::time_t local_timestamp = std::mktime(&tm_curr);
    if (local_timestamp < 0) {
        mclog::tagError(_tag, "failed to convert local time to timestamp");
        return false;
    }

    struct timeval tv = {
        .tv_sec  = local_timestamp,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        mclog::tagError(_tag, "failed to set system time");
        return false;
    }

    GetHAL().syncSystemTimeToRtc();
    return true;
}

}  // namespace

void Hal::rtc_init()
{
    mclog::tagInfo(_tag, "init");

    _rx8130 = std::make_unique<RX8130_Class>();
    if (!_rx8130->begin(_i2c_bus)) {
        _rx8130.reset();
        mclog::tagError(_tag, "init failed");
        return;
    }

    // Load timezone from settings
    std::string tz = getTimezone();
    setenv("TZ", tz.c_str(), 1);
    tzset();
    mclog::tagInfo(_tag, "load timezone from nvs: {}", tz);

    syncRtcTimeToSystem();
}

void Hal::syncRtcTimeToSystem()
{
    if (!_rx8130) {
        return;
    }

    std::lock_guard<std::mutex> lock(rtc_mutex);
    struct tm tm_curr = {};
    if (!_rx8130->getTime(&tm_curr)) {
        mclog::tagWarn(_tag, "RTC unavailable or invalid; waiting for network time");
        return;
    }
    // RTC holds UTC. Convert without changing process-wide TZ while UI reads it.
    int64_t days = 10957; // 1970-01-01 to 2000-01-01
    for (int year = 2000; year < tm_curr.tm_year + 1900; ++year)
        days += DateYmd::isLeapYear(year) ? 366 : 365;
    for (int month = 1; month <= tm_curr.tm_mon; ++month)
        days += DateYmd::daysInMonth(tm_curr.tm_year + 1900, month);
    days += tm_curr.tm_mday - 1;
    const time_t timestamp = days * 86400 + tm_curr.tm_hour * 3600 + tm_curr.tm_min * 60 + tm_curr.tm_sec;
    struct timeval tv = {.tv_sec = timestamp, .tv_usec = 0};
    if (settimeofday(&tv, nullptr) != 0) {
        mclog::tagError(_tag, "RTC restore failed");
        return;
    }
    mclog::tagInfo(_tag, "rtc synced to system (UTC): {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                   tm_curr.tm_year + 1900, tm_curr.tm_mon + 1, tm_curr.tm_mday, tm_curr.tm_hour, tm_curr.tm_min,
                   tm_curr.tm_sec);
}

void Hal::syncSystemTimeToRtc()
{
    if (!_rx8130) {
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_curr;
    gmtime_r(&tv.tv_sec, &tm_curr);

    std::lock_guard<std::mutex> lock(rtc_mutex);
    if (!_rx8130->setTime(&tm_curr)) {
        rtc_write_pending = true;
        mclog::tagError(_tag, "RTC write failed; will retry");
        return;
    }

    mclog::tagInfo(_tag, "system synced to rtc (UTC): {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                   tm_curr.tm_year + 1900, tm_curr.tm_mon + 1, tm_curr.tm_mday, tm_curr.tm_hour, tm_curr.tm_min,
                   tm_curr.tm_sec);
}

void Hal::setTimezone(std::string_view tz)
{
    const std::string timezone(tz);
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    Settings settings("system", true);
    settings.SetString("tz", std::string(tz));
    mclog::tagInfo(_tag, "timezone updated to: {}", tz);
}

std::string Hal::getTimezone()
{
    Settings settings("system", false);
    return settings.GetString("tz", "CST-8");
}

DateYmd Hal::getDateYmd()
{
    struct tm tm_curr;
    if (!_get_local_time(tm_curr)) {
        mclog::tagError(_tag, "get date failed");
        return DateYmd{};
    }

    return DateYmd{
        .year  = static_cast<uint16_t>(tm_curr.tm_year + 1900),
        .month = static_cast<uint8_t>(tm_curr.tm_mon + 1),
        .day   = static_cast<uint8_t>(tm_curr.tm_mday),
    };
}

bool Hal::setDateYmd(const DateYmd& date)
{
    if (!date.isValid()) {
        mclog::tagError(_tag, "set date failed, invalid date: {:04d}-{:02d}-{:02d}", date.year, date.month, date.day);
        return false;
    }

    struct tm tm_curr;
    if (!_get_local_time(tm_curr)) {
        mclog::tagError(_tag, "set date failed, current time unavailable");
        return false;
    }

    tm_curr.tm_year  = static_cast<int>(date.year) - 1900;
    tm_curr.tm_mon   = static_cast<int>(date.month) - 1;
    tm_curr.tm_mday  = static_cast<int>(date.day);
    tm_curr.tm_isdst = -1;

    bool ok = _apply_local_time(tm_curr);
    if (ok) {
        mclog::tagInfo(_tag, "date updated to {:04d}-{:02d}-{:02d}", date.year, date.month, date.day);
    }
    return ok;
}

TimeHms Hal::getTimeHms()
{
    struct tm tm_curr;
    if (!_get_local_time(tm_curr)) {
        mclog::tagError(_tag, "get time failed");
        return TimeHms{};
    }

    return TimeHms{
        .hour   = static_cast<uint8_t>(tm_curr.tm_hour),
        .minute = static_cast<uint8_t>(tm_curr.tm_min),
        .second = static_cast<uint8_t>(tm_curr.tm_sec),
    };
}

bool Hal::setTimeHms(const TimeHms& time)
{
    if (!time.isValid()) {
        mclog::tagError(_tag, "set time failed, invalid time: {:02d}:{:02d}:{:02d}", time.hour, time.minute,
                        time.second);
        return false;
    }

    struct tm tm_curr;
    if (!_get_local_time(tm_curr)) {
        mclog::tagError(_tag, "set time failed, current date unavailable");
        return false;
    }

    tm_curr.tm_hour  = static_cast<int>(time.hour);
    tm_curr.tm_min   = static_cast<int>(time.minute);
    tm_curr.tm_sec   = static_cast<int>(time.second);
    tm_curr.tm_isdst = -1;

    bool ok = _apply_local_time(tm_curr);
    if (ok) {
        mclog::tagInfo(_tag, "time updated to {:02d}:{:02d}:{:02d}", time.hour, time.minute, time.second);
    }
    return ok;
}

void Hal::rtc_tick()
{
    static uint32_t last_tick = 0;
    const uint32_t now = millis();
    if (now - last_tick < 1000) return;
    last_tick = now;
    // I2C runs here on the main task, outside LVGL and the TCP/IP callback.
    if (rtc_write_pending.exchange(false)) syncSystemTimeToRtc();
    const auto network = getWifiStatus();
    const bool connected = network.connected && !network.ipAddress.empty() && !network.configMode;
    if (!connected) {
        if (sntp_started) {
            esp_netif_sntp_deinit();
            sntp_started = false;
        }
        return;
    }
    if (sntp_started) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3,
        ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org"));
    config.wait_for_sync = false;
    config.sync_cb = [](struct timeval*) { rtc_write_pending = true; };
    const esp_err_t result = esp_netif_sntp_init(&config);
    if (result == ESP_OK) {
        sntp_started = true;
        ESP_LOGI("HAL-RTC", "Network time sync started; successful updates will be saved to RX8130 (UTC)");
    } else {
        ESP_LOGW("HAL-RTC", "SNTP start failed: %s", esp_err_to_name(result));
    }
}
