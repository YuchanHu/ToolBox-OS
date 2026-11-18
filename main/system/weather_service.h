#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace toolbox {
struct WeatherCity {
    std::string id, name, province, latitude, longitude, timezone;
    int offset = 28800;
};
struct WeatherDay {
    std::string date, temperature, high, low, condition, humidity, wind, precipitation, probability, updated;
    int code = 999;
    bool valid = false, night = false;
};
struct WeatherResult {
    WeatherCity city;
    std::array<WeatherDay, 2> days;
    std::vector<WeatherCity> cities;
    std::string message;
    bool busy = false, cached = false;
    uint32_t revision = 0;
};
class WeatherService {
  public:
    WeatherService();
    ~WeatherService();
    void search(const std::string &query);
    void refresh(const WeatherCity &city);
    WeatherResult snapshot();

  private:
    struct Request;
    WeatherResult result_;
    std::shared_ptr<Request> request_;
    void launch(const std::string &query, const WeatherCity &city);
    void start_pending();
};
} // namespace toolbox
