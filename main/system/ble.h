#pragma once
#include <esp_err.h>
namespace toolbox {
esp_err_t set_ble_enabled(bool enabled);
bool ble_enabled();
esp_err_t ble_error();
}
