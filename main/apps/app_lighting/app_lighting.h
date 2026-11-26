#pragma once
#include "parameter_picker.h"
#include <apps/common/key_manager/key_manager.h>
#include <atomic>
#include <mooncake.h>
#include <system/lighting/radio.h>

class AppLighting : public mooncake::AppAbility {
  public:
    AppLighting();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

  private:
    enum class Page { Lamps, Brands, Scan, Address, Control, Color, Channel, Delete, Effects };
    enum Action {
        None,
        Back,
        Add,
        Select,
        Refresh,
        Pair,
        Power,
        Brightness,
        Kelvin,
        Hue,
        Saturation,
        Tint,
        Color,
        Cct,
        Channel,
        Group,
        Confirm,
        Remove,
        Resend,
        Effect,
        ModeMenu
    };
    struct Saved {
        toolbox::lighting::Device device;
        toolbox::lighting::State state;
    };
    struct Store {
        uint32_t version = 1;
        uint16_t controller = 0;
        uint8_t next_group = 2;
        uint8_t count = 0;
        Saved lamps[8];
    } data_;
    Page page_ = Page::Lamps;
    int selected_ = -1;
    toolbox::lighting::Brand brand_ = toolbox::lighting::Brand::Inkee;
    toolbox::lighting::Device temp_{};
    toolbox::lighting::State desired_{};
    bool reconnect_ = false, is_new_ = false, dirty_ = false, deferred_send_ = false;
    uint32_t redraw_at_ = 0, send_at_ = 0;
    input::KeyManager keys_;
    std::atomic<uint32_t> pending_action_{0};
    LightingParameterPicker brightness_, temperature_;
    lv_obj_t *root_ = nullptr, *status_ = nullptr, *list_ = nullptr, *power_ = nullptr, *v1_ = nullptr,
             *v2_ = nullptr, *v3_ = nullptr, *s1_ = nullptr, *s2_ = nullptr;
    std::vector<toolbox::lighting::Device> shown_;
    void render();
    void menu_entry(const char* caption, int index, Action action, int value = 0);
    void bind_action(lv_obj_t* object, Action action, int value);
    void render_control();
    void render_values();
    void update_scan(const toolbox::lighting::RadioState &status);
    void begin_brand(toolbox::lighting::Brand brand);
    void selected_device(const toolbox::lighting::Device &device);
    void connected(const toolbox::lighting::RadioState &status);
    void handle(Action action, int value);
    void change();
    void save();
    void go_back();
    lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, Action action,
                     int value = 0, bool accent = false);
    lv_obj_t *slider(int x, int y, int w, int min, int max, int value, Action action);
    static void event(lv_event_t *event);
};
