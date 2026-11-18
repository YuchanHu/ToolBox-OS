#include "runtime.h"
#include "fonts.h"
#include "ble.h"
#include "shell.h"
#include "camera_remote.h"
#include <hal/utils/settings/settings.h>
#include <hal/hal.h>
#include <hal/refresh_metrics.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.hpp>
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace toolbox {

Runtime& runtime()
{
    static Runtime instance;
    return instance;
}

void Runtime::init()
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);
    commands_ = xQueueCreate(16, sizeof(Command));
    configASSERT(commands_);
    GetHAL().init();
    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    smooth_ui_toolkit::ui_hal::on_get_tick([]() { return GetHAL().millis(); });
    {
        LvglLockGuard lock;
        lv_obj_set_style_text_font(lv_screen_active(), fonts::body(), 0);
        lv_obj_set_style_text_font(lv_layer_top(), fonts::body(), 0);
        shell_init();
    }
    install_apps();
    ESP_LOGI("ToolBox", "1.1 ready; internal free=%u, PSRAM free=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

bool Runtime::post(Command command)
{
    return xQueueSend(commands_, &command, 0) == pdTRUE;
}

void Runtime::update()
{
    // Bound work per iteration so queued controls cannot starve app updates.
    Command brightness{CommandType::Brightness, -1, false};
    Command volume{CommandType::Volume, -1, false};
    auto apply_levels = [&] {
        if (brightness.value >= 0)
            GetHAL().setBackLightBrightness(brightness.value, brightness.save);
        if (volume.value >= 0)
            GetHAL().setSpeakerVolume(volume.value, volume.save);
        brightness.value = volume.value = -1;
        brightness.save = volume.save = false;
    };
    Command command;
    for (int count = 0; count < 16 && xQueueReceive(commands_, &command, 0) == pdTRUE; ++count) {
        if (command.type == CommandType::Brightness || command.type == CommandType::Volume) {
            auto& level = command.type == CommandType::Brightness ? brightness : volume;
            if (level.save && !command.save) apply_levels();
            level.value = command.value;
            level.save |= command.save;
            continue;
        }
        apply_levels();
        switch (command.type) {
        case CommandType::HomeTimeout:
        case CommandType::PowerTimeout: {
            Settings settings("toolbox", true);
            settings.SetInt(command.type == CommandType::HomeTimeout ? "home_secs" : "power_secs", command.value);
            break;
        }
        case CommandType::Ble:
            set_ble_enabled(command.value != 0);
            break;
        case CommandType::Provision:
            if (command.value) GetHAL().startWifiProvisioning();
            else GetHAL().stopWifiProvisioning();
            break;
        case CommandType::Wifi:
            GetHAL().setWifiEnabled(command.value != 0, command.save);
            break;
        case CommandType::Brightness:
        case CommandType::Volume:
            break;
        case CommandType::Face: {
            Settings settings("toolbox", true);
            settings.SetInt("analog", command.value);
            break;
        }
        }
    }
    apply_levels();
    GetHAL().feedTheDog();
    camera::tick();
    mooncake::GetMooncake().update();
#ifdef CONFIG_TOOLBOX_PERF
    auto& display = GetHAL().getDisplay();
    perf::report(display.width() * display.height());
#endif
    static uint32_t last_shutdown_attempt = 0;
    const uint32_t now = GetHAL().millis();
    if (shell_power_timeout() && shell_idle_ms() >= static_cast<uint32_t>(shell_power_timeout()) * 1000 &&
        now - last_shutdown_attempt >= 5000) {
        last_shutdown_attempt = now;
        GetHAL().powerOff();
    }
}

} // namespace toolbox
