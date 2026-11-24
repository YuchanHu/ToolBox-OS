#include "system/runtime.h"
#include <esp_log.h>
#include <esp_timer.h>

extern "C" void app_main(void)
{
    ESP_LOGI("Boot", "app_main_us=%lld", esp_timer_get_time());
    auto& system = toolbox::runtime();
    system.init();
    while (true) {
        system.update();
    }
}
