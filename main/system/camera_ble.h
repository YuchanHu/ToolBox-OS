#pragma once
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>

namespace toolbox::camera {
esp_err_t stack_started();
void stack_stopping();
void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* data);
void gatt_event(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t* data);
}
