#include "app_setup.h"
#include <system/shell.h>
#include <system/app_icons.h>
AppSetup::AppSetup() { setAppInfo().name = "设置"; setAppInfo().icon = (void*)&toolbox_app_settings; }
void AppSetup::onCreate() {}
void AppSetup::onOpen()
{
    done_ = false;
    _key_manager = std::make_unique<input::KeyManager>();
    LvglLockGuard lock;
    toolbox::shell_open_settings([this]() { done_ = true; });
}
void AppSetup::onRunning()
{
    if (done_ || _key_manager->update() == input::KeyEvent::GoHome) close();
}
void AppSetup::onClose()
{
    LvglLockGuard lock;
    toolbox::shell_close_settings();
    _key_manager.reset();
}
