#include "ble.h"
#include "camera_ble.h"
#include "lighting/radio.h"
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <atomic>

namespace toolbox {
namespace {
std::atomic<bool> enabled{false};
std::atomic<esp_err_t> error{ESP_OK};
esp_ble_adv_params_t parameters = {};

void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* data)
{
    if (lighting::owns_radio()) { lighting::gap_event(event, data); return; }
    camera::gap_event(event, data);
    if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT && enabled) {
        if (data->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            error = esp_ble_gap_start_advertising(&parameters);
        } else {
            error = ESP_FAIL;
        }
    }
    if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT && data->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        error = ESP_FAIL;
    }
}
void gatt_event(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t* data) {
    const bool housekeeping = event == ESP_GATTC_REG_EVT || event == ESP_GATTC_DISCONNECT_EVT || event == ESP_GATTC_CANCEL_OPEN_EVT;
    const bool lights = lighting::owns_radio();
    if (!lights || housekeeping) camera::gatt_event(event, interface, data);
    if (lights || housekeeping) lighting::gatt_event(event, interface, data);
}
}

void resume_idle_ble_advertising() {
    if (!enabled) return;
    esp_ble_adv_data_t data = {};
    data.include_name = true;
    data.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    esp_ble_gap_config_adv_data(&data);
}

bool ble_enabled() { return enabled.load(); }
esp_err_t ble_error() { return error.load(); }

esp_err_t set_ble_enabled(bool target)
{
    // Runtime serializes all controller transitions on the main task.
    if (target == enabled && error == ESP_OK) return ESP_OK;
    enabled = false;
    lighting::stack_stopping();
    camera::stack_stopping();
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_ble_gap_stop_advertising();
        error = esp_bluedroid_disable();
        if (error != ESP_OK) return error;
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        error = esp_bt_controller_disable();
        if (error != ESP_OK) return error;
    }
    if (!target) return error = ESP_OK;
    ESP_LOGI("ToolBox-BLE", "starting: internal free=%u, largest=%u, PSRAM free=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
        // The controller ROM cleanup can fault after partial allocation failure.
        // Keep headroom for controller allocations and concurrent system tasks.
        constexpr size_t minimum_free = 48 * 1024;
        constexpr size_t minimum_block = 16 * 1024;
        const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        if (heap_caps_get_free_size(caps) < minimum_free ||
            heap_caps_get_largest_free_block(caps) < minimum_block) {
            ESP_LOGE("ToolBox-BLE", "Insufficient internal memory for controller startup");
            return error = ESP_ERR_NO_MEM;
        }
        esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        error = esp_bt_controller_init(&config);
        if (error != ESP_OK) return error;
    }
    error = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (error != ESP_OK) return error;
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        error = esp_bluedroid_init();
        if (error != ESP_OK) {
            ESP_LOGE("ToolBox-BLE", "Bluedroid init failed: %s", esp_err_to_name(error));
            return error;
        }
    }
    error = esp_bluedroid_enable();
    if (error != ESP_OK) return error;
    error = esp_ble_gap_register_callback(gap_event);
    if (error != ESP_OK) return error;
    error = esp_ble_gattc_register_callback(gatt_event);
    if (error != ESP_OK) return error;
    error = camera::stack_started();
    if (error != ESP_OK) return error;
    error = lighting::stack_started();
    if (error != ESP_OK) return error;
    error = esp_ble_gap_set_device_name("ToolBox OS");
    if (error != ESP_OK) return error;
    parameters.adv_int_min = 0x100;
    parameters.adv_int_max = 0x180;
    parameters.adv_type = ADV_TYPE_NONCONN_IND;
    parameters.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    parameters.channel_map = ADV_CHNL_ALL;
    parameters.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    esp_ble_adv_data_t data = {};
    data.include_name = true;
    data.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    enabled = true;
    error = esp_ble_gap_config_adv_data(&data);
    if (error != ESP_OK) enabled = false;
    ESP_LOGI("ToolBox-BLE", "enable result=%s", esp_err_to_name(error));
    return error;
}
}
