#include "system/runtime.h"

extern "C" void app_main(void)
{
    auto& system = toolbox::runtime();
    system.init();
    while (true) {
        system.update();
    }
}
