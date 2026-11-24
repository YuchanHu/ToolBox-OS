#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace toolbox {
struct WeatherCity {
    // mode and input identify the configured query. name comes from the last response.
    std::string mode = "auto", input, name;
};
struct WeatherDay {
    std::string date, week, temperature, high, low, condition, day_condition, night_condition, humidity, wind, precipitation, probability, updated;
    std::string direction, power, uv, sunrise, sunset, feels_like, visibility, pressure, cloud, aqi, category, pollutants, warning;
    int code = 999;
    bool valid = false, night = false;
};
struct WeatherHour {
    std::string time, temperature, condition, humidity, direction, speed, pop, precip, feels_like;
};
struct Forecast {
    std::vector<WeatherDay> days;
    std::vector<WeatherHour> hours;
};
struct WeatherResult {
    WeatherCity city;
    std::shared_ptr<const Forecast> forecast;
    const WeatherDay& day(size_t index) const {
        static const WeatherDay empty{};
        return forecast && index < forecast->days.size() ? forecast->days[index] : empty;
    }
    std::vector<WeatherCity> cities;
    std::string message;
    bool busy = false, cached = false;
    int64_t fetched_at = 0;
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
