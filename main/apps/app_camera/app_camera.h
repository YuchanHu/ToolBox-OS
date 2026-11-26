#pragma once
#include <mooncake.h>
#include <system/camera_ui.h>
#include <memory>
#include <apps/common/key_manager/key_manager.h>
#include <sony/connection_ui.h>

class AppCamera : public mooncake::AppAbility {
public:
    AppCamera();
    void onCreate() override {}
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    void openBluetooth();
    std::unique_ptr<toolbox::CameraConnectPage> connect_;
    std::unique_ptr<toolbox::CameraPage> view_;
    toolbox::CameraPage::Screen screen_ = toolbox::CameraPage::Screen::Modes;
    uint32_t polled_at_ = 0;
    uint32_t recording_activity_at_ = 0;
    bool stopping_movie_ = false;
    bool sonyChord_ = false;
    input::KeyManager keys_;
};
