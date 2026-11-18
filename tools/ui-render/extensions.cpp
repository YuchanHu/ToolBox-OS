#include <apps/app_counter/app_counter.h>
#include <apps/app_timers/app_timers.h>
#include <apps/app_focus/app_focus.h>
#include <apps/app_level/app_level.h>
#include <apps/app_weather/app_weather.h>
#include <cJSON.h>
#include <cmath>
#include <fstream>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <iostream>
#include <iterator>
#include <system/weather_decode.h>

extern void advance(unsigned);
extern void screenshot(const char *);
extern void require(bool, const char *);
extern lv_obj_t *find_text(lv_obj_t *, const char *);

namespace audio {
void play_tone(int, float, float) {}
void stop() {}
} // namespace audio
namespace toolbox {
WeatherService::WeatherService() {
    result_.message = "选择城市";
}
WeatherService::~WeatherService() {}
WeatherResult WeatherService::snapshot() {
    return result_;
}
void WeatherService::search(const std::string &) {}
void WeatherService::refresh(const WeatherCity &) {}
} // namespace toolbox
namespace {
void click(const char *text) {
    auto *caption = find_text(lv_screen_active(), text);
    require(caption != nullptr, text);
    lv_obj_send_event(lv_obj_get_parent(caption), LV_EVENT_CLICKED, nullptr);
}
void poll(mooncake::AppAbility &app, bool a, bool b, int duration = 80) {
    GetHAL().rawA = a;
    GetHAL().rawB = b;
    for (int t = 0; t < duration; t += 10) {
        advance(10);
        app.baseUpdate();
    }
}
void key(mooncake::AppAbility &app, bool a, int duration = 50) {
    poll(app, a, !a, duration);
    poll(app, false, false);
}
void open(mooncake::AppAbility &app) {
    GetHAL().rawA = GetHAL().rawB = false;
    app.baseCreate();
    app.open();
    app.baseUpdate();
    poll(app, false, false);
}
void close(mooncake::AppAbility &app) {
    app.close();
    app.baseUpdate();
}
} // namespace
void check_extensions() {
    using namespace toolbox;
    Countdown timer;
    timer.reset(1000000);
    timer.toggle(0);
    timer.tick(200000);
    timer.toggle(200000);
    require(timer.remaining == 800000 && timer.state == Countdown::State::Paused,
            "Monotonic pause must retain exact remaining time");
    timer.toggle(10000000);
    require(!timer.tick(10799999) && timer.tick(10800000) && !timer.tick(12000000), "Completion must happen once");
    timer.reset(timer.duration);
    require(timer.remaining == 1000000 && timer.state == Countdown::State::Ready, "Reset restores original duration");
    LevelReading level;
    double bias[3]{}, target[3]{0, 0, 1};
    level.sample(.1, .2, .974679434, bias, target, true);
    require(level.valid && std::abs(level.tilt - 12.920966) < .01, "Level total inclination must use both axes");
    const double pin[] = {level.x, level.y, level.z};
    level.sample(.1, .2, .974679434, bias, pin, true);
    require(level.error < .001, "PIN must compare full gravity direction");
    level.sample(0, 0, 0, bias, target, true);
    require(!level.valid, "Invalid gravity cannot become a level reading");

    for (auto kind : {Extension::Level, Extension::Focus, Extension::Timers, Extension::Counter, Extension::Weather}) {
        ExtensionViewState state;
        state.kind = kind;
        state.count = 128;
        state.time = kind == Extension::Focus ? "18:42" : "06:32";
        state.detail = kind == Extension::Focus ? "专注 · 第 1 / 4 轮" : "10 分钟";
        state.progress = .65;
        state.timer = Countdown::State::Running;
        state.valid = true;
        state.angle = 2.35;
        state.x = 1.41;
        state.y = -1.88;
        state.direction = -40;
        state.error = 2.35;
        const char *names[] = {"counter-main", "timers-run", "focus-run", "level-live", "weather-empty"};
        ExtensionView view(kind);
        view.update(state, lv_tick_get());
        advance(40);
        screenshot(names[static_cast<int>(kind)]);
        if (kind == Extension::Level || kind == Extension::Focus || kind == Extension::Timers) {
            require(!view.controls_visible(), "Main controls must start hidden");
            view.reveal(lv_tick_get());
            view.update(state, lv_tick_get());
            advance(40);
            screenshot((std::string(names[static_cast<int>(kind)]) + "-controls").c_str());
            advance(2100);
            view.update(state, lv_tick_get());
            require(view.controls_visible(), "Controls must fade after two seconds");
            advance(250);
            view.update(state, lv_tick_get());
            require(!view.controls_visible(), "Faded controls must leave hit testing");
        }
        if (kind == Extension::Level) {
            state.error = 0;
            state.angle = 0;
            view.update(state, lv_tick_get());
            advance(40);
            screenshot("level-zero");
            state.pin = true;
            state.target = 12;
            state.angle = 12;
            view.update(state, lv_tick_get());
            advance(40);
            screenshot("level-pin");
        }
        if (kind == Extension::Counter) {
            state.confirm = true;
            view.update(state, lv_tick_get());
            screenshot("counter-confirm");
        }
        if (kind == Extension::Focus) {
            state.confirm = true;
            view.update(state, lv_tick_get());
            screenshot("focus-confirm");
            state.confirm = false;
            state.page = ExtensionPage::Settings;
            view.update(state, lv_tick_get());
            screenshot("focus-settings");
        }
        if (kind == Extension::Timers) {
            state.page = ExtensionPage::Presets;
            view.update(state, lv_tick_get());
            screenshot("timers-presets");
            state.page = ExtensionPage::Custom;
            state.values = {0, 10, 0};
            view.update(state, lv_tick_get());
            screenshot("timers-custom");
        }
        if (kind == Extension::Weather) {
            state.page = ExtensionPage::City;
            view.update(state, lv_tick_get());
            auto* root=lv_obj_get_child(lv_screen_active(),-1);
            auto* content=lv_obj_get_child(root,0);
            lv_obj_t* keyboard=nullptr;
            for(uint32_t i=0;i<lv_obj_get_child_count(content);++i){
                auto* child=lv_obj_get_child(content,i);
                if(lv_obj_check_type(child,&lv_keyboard_class))keyboard=child;
            }
            require(keyboard!=nullptr,"City search must have a visible keyboard");
            lv_obj_update_layout(keyboard);lv_area_t bounds;lv_obj_get_coords(keyboard,&bounds);
            require(bounds.y1>=300&&bounds.y2<466,"Keyboard must stay within circular screen height");
            lv_buttonmatrix_set_selected_button(keyboard,10);
            lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);
            auto typed=view.take_event();
            require(typed.action==ExtensionAction::Input&&typed.text=="q","Keyboard must insert each character once");
            lv_buttonmatrix_set_selected_button(keyboard,38);
            lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);
            require(view.take_event().action==ExtensionAction::Search,"Keyboard OK starts city search");
            screenshot("weather-city");
        }
    }
    {
        std::ifstream input(TOOLBOX_UI_SOURCE_DIR "/fixtures/weather-uapi.json");
        std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        auto *json = cJSON_Parse(body.c_str());
        require(json != nullptr, "Real domestic API response fixture missing");
        ExtensionViewState state;
        state.kind = Extension::Weather;
        state.weather.city.name = "北京";
        state.weather.days[0].date = "2026-11-17";
        state.weather.days[1].date = "2026-11-18";
        require(decode_weather(state.weather, json, 12 * 60), "Production parser must decode real domestic response");
        cJSON_Delete(json);
        require(state.weather.days[0].temperature == "7" && state.weather.days[1].high == "7" &&
                    state.weather.days[0].probability.empty(),
                "Missing probability cannot become fabricated data");
        ExtensionView view(Extension::Weather);
        view.update(state, lv_tick_get());
        advance(40);
        screenshot("weather-today");
        state.day = 1;
        view.update(state, lv_tick_get());
        advance(200);
        screenshot("weather-tomorrow");
        state.day = 0;
        state.weather.days[0].code = 100;
        state.weather.days[0].condition = "晴";
        view.update(state, lv_tick_get());
        advance(200);
        screenshot("weather-sun");
        for (int code : {103, 104, 501, 309, 305, 307, 313, 400, 404, 302, 304}) {
            state.weather.days[0].code = code;
            view.update(state, lv_tick_get());
            advance(40);
            screenshot(("weather-animation-" + std::to_string(code)).c_str());
        }
        state.weather.days[0].night = true;
        state.weather.days[0].code = 100;
        view.update(state, lv_tick_get());
        advance(40);
        screenshot("weather-night");
    }
    level.sample(0, 0, 1, bias, target, true);
    for (int i = 0; i < 12; ++i)
        level.sample(0, 0, -1, bias, target, false);
    require(level.tilt > 179, "Gravity filter must follow an upside-down transition");
    // Actual Mooncake lifecycle + real button debouncer, without radio or sensor fixtures in production.
    Settings settings("extensions", true);
    settings.SetInt("count", 0);
    {
        AppCounter app;
        open(app);
        const int initial = GetHAL().vibration_count;
        key(app, false);
        require(find_text(lv_screen_active(), "1") && GetHAL().vibration_count == initial + 1,
                "B adds once and vibrates once");
        key(app, true, 650);
        require(find_text(lv_screen_active(), "确定重置计数？"), "Hold A opens confirmation");
        key(app, true);
        require(find_text(lv_screen_active(), "1"), "Physical key must not confirm reset");
        click("确定重置");
        poll(app, false, false);
        require(find_text(lv_screen_active(), "0"), "Screen confirm resets counter");
        key(app, true);
        require(GetHAL().vibration_count == initial + 3, "Zero boundary is not a valid decrement");
        key(app, false);
        close(app);
    }
    {
        AppCounter app;
        open(app);
        require(find_text(lv_screen_active(), "1"), "Count survives reopen");
        close(app);
    }
    {
        AppTimers app;
        open(app);
        click("1 分钟");
        poll(app, false, false);
        key(app, false);
        poll(app, false, false, 1500);
        key(app, false);
        require(find_text(lv_screen_active(), "00:59"), "B pauses elapsed timer");
        poll(app, false, false, 1200);
        require(find_text(lv_screen_active(), "00:59"), "Paused timer stays fixed");
        key(app, true);
        require(find_text(lv_screen_active(), "01:00"), "A restores original timer immediately");
        close(app);
    }
    {
        AppFocus app;
        open(app);
        key(app, false);
        poll(app, false, false, 1200);
        key(app, false);
        key(app, true);
        require(find_text(lv_screen_active(), "再次按 KEYA 退出专注"), "First A requests focus exit");
        poll(app, false, false, 1100);
        key(app, true);
        require(find_text(lv_screen_active(), "25:00"), "Second A exits focus");
        require(settings.GetInt("completed", 0) == 0, "Abandoned focus cannot count as completed");
        for (int round = 0; round < 4; ++round) {
            key(app, false);
            lv_tick_inc(25 * 60 * 1000);
            app.baseUpdate();
            poll(app, false, false, 1100);
            require(settings.GetInt("completed", 0) == round + 1, "Natural focus completion counted exactly once");
            if (round < 3) {
                click("开始休息");
                poll(app, false, false);
                lv_tick_inc(5 * 60 * 1000);
                app.baseUpdate();
                poll(app, false, false);
            }
        }
        require(find_text(lv_screen_active(), "15:00"), "Fourth focus schedules long rest");
        close(app);
    }
    {
        AppWeather app;
        open(app);
        const int feedback = GetHAL().vibration_count;
        key(app, true);
        require(GetHAL().vibration_count == feedback, "Today boundary has no extra key feedback");
        key(app, false);
        require(GetHAL().vibration_count == feedback + 1, "B moves to tomorrow once");
        key(app, false);
        require(GetHAL().vibration_count == feedback + 1, "Tomorrow boundary cannot advance");
        key(app, true);
        require(GetHAL().vibration_count == feedback + 2, "A returns to today");
        close(app);
    }
    {
        AppCounter app;
        open(app);
        const int feedback = GetHAL().vibration_count;
        poll(app, true, true, 650);
        poll(app, false, false);
        require(app.currentState() == mooncake::AppAbility::StateSleeping && GetHAL().vibration_count == feedback,
                "Home chord outranks counter long reset without single-key feedback");
    }
    {
        AppTimers app;
        open(app);
        GetHAL().brightness = 0;
        poll(app, false, false);
        require(app.currentState() == mooncake::AppAbility::StateSleeping && !GetHAL().applicationButtonFeedback,
                "Screen off closes foreground business");
        GetHAL().brightness = 65;
    }
    {
        AppLevel app;
        open(app);
        poll(app, false, false, 120);
        key(app, true, 650);
        require(!find_text(lv_screen_active(), "确定重置计数？"), "Level hold must use relative zero action");
        close(app);
    }
    require(!GetHAL().applicationButtonFeedback, "Close must restore legacy hardware feedback");
    std::cout << "Passed five-app production UI rendering, hidden controls, monotonic timer, two-axis/PIN, counter "
                 "persistence and physical key workflows\n";
}
