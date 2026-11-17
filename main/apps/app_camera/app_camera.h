#pragma once
#include <mooncake.h>
#include <system/camera_ui.h>
#include <memory>
#include <apps/common/key_manager/key_manager.h>

class AppCamera : public mooncake::AppAbility {
public:
    AppCamera();
    void onCreate() override {}
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
private:
    std::unique_ptr<toolbox::CameraPage> view_;
    toolbox::CameraPage::Screen screen_ = toolbox::CameraPage::Screen::Modes;
    uint32_t updated_at_ = 0;
    input::KeyManager keys_;
};
