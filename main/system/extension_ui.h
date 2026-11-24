#pragma once
#include "extension_models.h"
#include "ui.h"
#include "weather_service.h"
#include <string>
#include <vector>

namespace toolbox {
enum class ExtensionPage { Main, Presets, Custom, Settings, Calibration, City, Details, Hourly, Daily, Stats };
enum class ExtensionAction {
    None,
    Back,
    Primary,
    Secondary,
    Settings,
    Cancel,
    Confirm,
    Custom,
    Save,
    Preset,
    Edit,
    Plus,
    Minus,
    Search,
    Input,
    City,
    Refresh,
    Calibrate,
    Sound,
    Zero,
    Stats
};
struct ExtensionEvent {
    ExtensionAction action = ExtensionAction::None;
    int value = 0;
    std::string text{};
};
struct ExtensionViewState {
    Extension kind = Extension::Counter;
    ExtensionPage page = ExtensionPage::Main;
    std::string time = "25:00", detail, message;
    uint64_t count = 0;
    Countdown::State timer = Countdown::State::Ready;
    double progress = 1;
    int round = 0, completed = 0;
    bool rest = false, confirm = false, frozen = false, sound = true, pin = false;
    double angle = 0, x = 0, y = 0, direction = 0, error = 180, tolerance = .1, target = 0;
    bool valid = false;
    int edit = 0;
    std::array<int, 3> values{25, 5, 15};
    WeatherResult weather;
    int day = 0;
};
class ExtensionView {
  public:
    explicit ExtensionView(Extension kind);
    ~ExtensionView();
    ExtensionEvent take_event();
    void update(const ExtensionViewState &state, uint32_t now);
    void reveal(uint32_t now);
    bool controls_visible() const {
        return controls_ && !lv_obj_has_flag(controls_, LV_OBJ_FLAG_HIDDEN);
    }

  private:
    Extension kind_;
    ExtensionViewState state_;
    ExtensionPage built_page_ = ExtensionPage::Main;
    bool built_ = false, built_confirm_ = false;
    lv_obj_t *root_, *content_ = nullptr, *controls_ = nullptr, *dialog_ = nullptr;
    lv_obj_t *time_ = nullptr, *detail_ = nullptr, *message_ = nullptr, *ring_ = nullptr, *drawing_ = nullptr;
    lv_obj_t *primary_ = nullptr, *input_ = nullptr;
    lv_obj_t *keyboard_=nullptr,*results_=nullptr;
    std::vector<lv_obj_t *> values_;
    ExtensionEvent pending_;
    uint32_t revealed_at_ = 0, frame_at_ = 0, city_revision_ = UINT32_MAX;
    lv_obj_t *sky_ = nullptr, *pull_hint_ = nullptr;
    lv_point_t pull_origin_{};
    bool pulling_ = false;
    std::vector<lv_point_precise_t> hourly_points_;
    void forecast_page();
    int previous_day_ = 0;
    std::array<int, 3> level_display_{};
    static void event(lv_event_t *event);
    static void draw(lv_event_t *event);
    void build();
    void confirmation();
    lv_obj_t *button(lv_obj_t *parent, const char *caption, int x, int y, int width, int height, ExtensionAction action,
                     int value = 0, bool accent = false);
};
} // namespace toolbox
