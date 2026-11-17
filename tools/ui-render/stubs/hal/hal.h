#pragma once
#include <lvgl.h>
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#include <hal/utils/button/Button_Class.hpp>
extern bool preview_analog;
inline std::tm* localtime_r(const std::time_t* time, std::tm* result)
{
    localtime_s(result, time);
    result->tm_year = 126; result->tm_mon = 10; result->tm_mday = 16; result->tm_wday = 1;
    result->tm_hour = preview_analog ? 10 : 18; result->tm_min = preview_analog ? 9 : 36; result->tm_sec = 30;
    return result;
}
struct Hal {
    void buttonFeedback(bool, bool = true) {}
    enum class WifiMode { Off, Connecting, Connected, ConfigAp, Error };
    struct WifiStatus {
        bool enabled = true, connected = true, configMode = false;
        WifiMode mode = WifiMode::Connected;
        std::string ssid = "Studio_2.4G", ipAddress = "192.168.1.108";
        std::string apSsid = "M5StopWatch-A1B2", apUrl = "http://192.168.4.1";
    };
    std::unique_ptr<int> bootLogo;
    lv_indev_t* lvTouchpad = nullptr;
    WifiStatus network;
    m5::Button_Class btnA, btnB;
    bool rawA = false, rawB = false;
    bool applicationButtonFeedback = false;
    int vibration_count=0;
    void vibrate(uint16_t,uint8_t=100){++vibration_count;}
    void stopVibrate(){}
    void audioPlay(std::vector<int16_t>&,bool=true){}
    struct ImuData {float accelX=0,accelY=0,accelZ=1;};
    ImuData imu;
    void updateImuData(){}
    ImuData getImuData(){return imu;}
    uint32_t millis() { return lv_tick_get(); }
    void updateButtonStates() { btnA.setRawState(millis(), rawA); btnB.setRawState(millis(), rawB); }
    int battery_percent = 82;
    int getBatteryLevel() { return battery_percent; }
    bool charging = false;
    bool isBatteryCharging(bool = false) { return charging; }
    bool isWifiEnabled() { return network.enabled; }
    WifiStatus getWifiStatus() { return network; }
    int brightness=65;
    int getBackLightBrightness() { return brightness; }
    int getSpeakerVolume() { return 40; }
};
Hal& GetHAL();
struct LvglLockGuard {};

