#include "app_launcher.h"
#include <hal/hal.h>
#include <system/shell.h>

void AppLauncher::onLauncherCreate() { open(); }

void AppLauncher::onLauncherOpen()
{
    LvglLockGuard lock;
    create_launcher_view();
    if (returning_) toolbox::shell_resume_drawer();
    toolbox::shell_attach(_view->root());
    returning_ = false;
}

void AppLauncher::onLauncherRunning()
{
    LvglLockGuard lock;
    if (toolbox::shell_is_locked()) GetHAL().updateButtonStates();
    if (toolbox::shell_home_timeout() && toolbox::shell_idle_ms() >= static_cast<uint32_t>(toolbox::shell_home_timeout()) * 1000) {
        toolbox::shell_close_settings();
        toolbox::shell_lock();
    }
    if (!toolbox::shell_is_locked()) _view->update();
}

void AppLauncher::onLauncherSleeping()
{
    if (getRunningAppId() >= 0 && toolbox::shell_home_timeout() &&
        toolbox::shell_idle_ms() >= static_cast<uint32_t>(toolbox::shell_home_timeout()) * 1000) {
        returning_ = false;
        mooncake::GetMooncake().closeApp(getRunningAppId());
    }
}

void AppLauncher::onLauncherClose()
{
    LvglLockGuard lock;
    toolbox::shell_detach();
    _view.reset();
}

void AppLauncher::onLauncherDestroy() {}

void AppLauncher::create_launcher_view()
{
    _view = std::make_unique<view::LauncherView>();
    _view->init(getAppProps());
    _view->onAppClicked = [this](int appID) { returning_ = true; openApp(appID); };
}
