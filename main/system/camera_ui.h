#pragma once
#include "camera_remote.h"
#include "ui.h"
#include <atomic>

namespace toolbox {
class CameraPage {
public:
    enum class Screen { Modes, Photo, Movie };
    enum class ActionType { None, Back, Rescan, Select, Photo, Movie };
    struct Action { ActionType type = ActionType::None; camera::Device device; };
    CameraPage();
    ~CameraPage();
    bool has_action() const { return action_ready_.load(); }
    Action take_action();
    bool needs_update(const camera::Status& status, Screen screen, uint32_t now, bool stopping) const;
    void update(const camera::Status& status, Screen screen, uint32_t now, bool stopping = false);
    static bool is_recording(const camera::Status& status);
private:
    enum class Scene {
        Search, Devices, Connecting, Pairing, Modes, PhotoReady, PhotoBusy, PhotoSent,
        MovieReady, MovieStart, MovieFinishing, Recording, MovieStop, Reconnect, Error, BluetoothOff
    };
    lv_obj_t *root_, *ring_, *title_, *back_, *clock_, *battery_, *battery_percent_;
    lv_obj_t *message_, *detail_, *devices_, *modes_, *symbol_, *time_, *hint_, *rescan_;
    Action pending_;
    std::atomic_bool action_ready_{false};
    std::vector<camera::Device> displayed_devices_;
    std::string previous_name_, previous_message_;
    Scene scene_ = Scene::Search;
    uint32_t previous_second_ = UINT32_MAX;
    uint32_t header_at_ = 0;
    bool initialized_ = false;
    bool previous_busy_ = false;
    static void event(lv_event_t* event);
    static Scene scene_for(const camera::Status& status, Screen screen, bool stopping);
    static uint32_t elapsed_seconds(const camera::Status& status, uint32_t now);
    bool devices_changed(const std::vector<camera::Device>& devices) const;
    void rebuild_devices(const camera::Status& status);
};
}
