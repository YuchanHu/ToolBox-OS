#include "runtime.h"
#include <apps/app_launcher/app_launcher.h>
#include <apps/app_stopwatch/app_stopwatch.h>
#include <apps/app_setup/app_setup.h>
#include <apps/app_camera/app_camera.h>
#include <apps/app_lighting/app_lighting.h>
#include <mooncake.h>
#include <apps/app_counter/app_counter.h>
#include <apps/app_timers/app_timers.h>
#include <apps/app_focus/app_focus.h>
#include <apps/app_level/app_level.h>
#include <apps/app_weather/app_weather.h>

namespace toolbox {

void install_apps()
{
    auto& apps = mooncake::GetMooncake();
    // Launcher owns foreground app navigation; services remain in Runtime/HAL.
    apps.installApp(std::make_unique<AppLauncher>());

    apps.installApp(std::make_unique<AppStopWatch>());
    apps.installApp(std::make_unique<AppSetup>());
    apps.installApp(std::make_unique<AppCamera>());
    apps.installApp(std::make_unique<AppLighting>());
    apps.installApp(std::make_unique<AppCounter>());
    apps.installApp(std::make_unique<AppTimers>());
    apps.installApp(std::make_unique<AppFocus>());
    apps.installApp(std::make_unique<AppLevel>());
    apps.installApp(std::make_unique<AppWeather>());
}

} // namespace toolbox
