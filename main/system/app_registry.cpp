#include "runtime.h"
#include <apps/app_launcher/app_launcher.h>
#include <apps/app_stopwatch/app_stopwatch.h>
#include <apps/app_setup/app_setup.h>
#include <apps/app_camera/app_camera.h>
#include <mooncake.h>
#include <apps/app_extensions/app_extensions.h>

namespace toolbox {

void install_apps()
{
    auto& apps = mooncake::GetMooncake();
    // Launcher owns foreground app navigation; services remain in Runtime/HAL.
    apps.installApp(std::make_unique<AppLauncher>());

    apps.installApp(std::make_unique<AppStopWatch>());
    apps.installApp(std::make_unique<AppSetup>());
    apps.installApp(std::make_unique<AppCamera>());
    for (auto kind : {Extension::Counter, Extension::Timers, Extension::Focus, Extension::Level, Extension::Weather})
        apps.installApp(std::make_unique<ExtensionApp>(kind));
}

} // namespace toolbox
