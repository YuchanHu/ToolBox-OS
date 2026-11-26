#include "system/runtime.h"
#include <esp_log.h>
#include <esp_timer.h>
#ifdef TOOLBOX_SONY_PROBE
#include "sony/probe.h"
#endif

extern "C" void app_main(void)
{
    ESP_LOGI("Boot", "app_main_us=%lld", esp_timer_get_time());
#ifdef TOOLBOX_SONY_PROBE
    sony::enterProbeOnRequest();
#endif
    auto& system = toolbox::runtime();
    system.init();
    while (true) {
        system.update();
    }
}
