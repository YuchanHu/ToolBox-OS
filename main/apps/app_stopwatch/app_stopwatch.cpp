#include "app_stopwatch.h"
#include <hal/hal.h>
#include <system/app_icons.h>
AppStopWatch::AppStopWatch() { setAppInfo().name = "秒表"; setAppInfo().icon = (void*)&toolbox_app_stopwatch; }
void AppStopWatch::onCreate() {}
void AppStopWatch::onOpen()
{
    done_ = false;
    _key_manager = std::make_unique<input::KeyManager>();
    LvglLockGuard lock;
    stopwatch_.reset();
    _view = std::make_unique<toolbox::StopwatchPage>([this]() { done_ = true; },
        [this]() { left_action(); }, [this]() { toggle_action(); });
}
void AppStopWatch::left_action()
{
    if (stopwatch_.getState() == model::Stopwatch::State_t::Running) stopwatch_.lap();
    else if (stopwatch_.getState() == model::Stopwatch::State_t::Paused) stopwatch_.reset();
}
void AppStopWatch::toggle_action()
{
    if (stopwatch_.getState() == model::Stopwatch::State_t::Running) stopwatch_.pause();
    else stopwatch_.start();
}
void AppStopWatch::onRunning()
{
    GetHAL().updateButtonStates();
    if (done_ || _key_manager->update(false) == input::KeyEvent::GoHome) { close(); return; }
    LvglLockGuard lock;
    if (GetHAL().btnA.wasPressed()) left_action();
    if (GetHAL().btnB.wasPressed()) toggle_action();
    _view->update(stopwatch_.getElapsedTime(), stopwatch_.getState(), stopwatch_.getLaps());
}
void AppStopWatch::onClose()
{
    LvglLockGuard lock;
    _view.reset();
    _key_manager.reset();
}
