#pragma once

#include <lvgl.h>

class LightingParameterPicker {
  public:
    using Commit = void (*)(void *, int);

    void create_brightness(lv_obj_t *parent, int x, int y, int value, bool decimal, Commit commit, void *context);
    void create_temperature(lv_obj_t *parent, int x, int y, int value, int minimum, int maximum,
                            int step, Commit commit, void *context);
    void set_value(int value);

  private:
    enum class Unit { Percent, DecimalPercent, Kelvin };
    struct Scale {
        Unit unit;
        int minimum;
        int maximum;
        int step;
        int pixels_per_step;
        int steps_per_tick;
        int major_interval;
    };

    void create(lv_obj_t *parent, int x, int y, Scale scale, Commit commit, void *context);
    void draw(lv_event_t *event) const;
    void touch(lv_event_code_t code);
    void move_to(int offset);
    int value_at(int index) const;
    int nearest_index(int value) const;
    int snapped_index() const;
    static void event(lv_event_t *event);

    lv_obj_t *area_ = nullptr;
    lv_obj_t *number_ = nullptr;
    Commit commit_ = nullptr;
    void *context_ = nullptr;
    Scale scale_{};
    bool pressed_ = false;
    int first_grid_ = 0, last_index_ = 0;
    int selected_index_ = 0, offset_ = 0, previous_point_ = 0;
    uint32_t previous_tick_ = 0;
};
