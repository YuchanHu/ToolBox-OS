#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace toolbox {

enum class CommandType { Wifi, Brightness, Volume, Face, Ble, Provision, HomeTimeout, PowerTimeout };

struct Command {
    CommandType type;
    int value;
    bool save;
};

// HAL and Mooncake live for the entire firmware session. UI callbacks enqueue
// hardware commands; the main task executes them outside the LVGL mutex.
class Runtime {
public:
    void init();
    void update();
    bool post(Command command);

private:
    QueueHandle_t commands_ = nullptr;
};

Runtime& runtime();
void install_apps();

} // namespace toolbox
