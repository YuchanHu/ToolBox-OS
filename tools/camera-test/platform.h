#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <cstring>
using esp_err_t = int;
using esp_gatt_if_t = int;
using esp_gatt_status_t = int;
using esp_gap_ble_cb_event_t = int;
using esp_gattc_cb_event_t = int;
using esp_ble_addr_type_t = int;
using esp_ble_auth_req_t = int;
using esp_ble_io_cap_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_GATT_OK = 0, ESP_GATT_IF_NONE = -1;
enum {
    ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, ESP_GAP_BLE_SCAN_START_COMPLETE_EVT,
    ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT, ESP_GAP_BLE_SCAN_RESULT_EVT, ESP_GAP_BLE_SEC_REQ_EVT,
    ESP_GAP_BLE_AUTH_CMPL_EVT, ESP_GAP_BLE_PASSKEY_REQ_EVT,
    ESP_GATTC_REG_EVT, ESP_GATTC_CANCEL_OPEN_EVT, ESP_GATTC_CONNECT_EVT, ESP_GATTC_OPEN_EVT,
    ESP_GATTC_DISCONNECT_EVT, ESP_GATTC_SEARCH_RES_EVT, ESP_GATTC_SEARCH_CMPL_EVT,
    ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_WRITE_DESCR_EVT, ESP_GATTC_NOTIFY_EVT, ESP_GATTC_WRITE_CHAR_EVT,
    BLE_SCAN_TYPE_ACTIVE, BLE_ADDR_TYPE_PUBLIC, BLE_SCAN_FILTER_ALLOW_ALL, BLE_SCAN_DUPLICATE_DISABLE,
    ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM, ESP_BLE_SEC_ENCRYPT_NO_MITM,
    ESP_LE_AUTH_BOND, ESP_IO_CAP_NONE, ESP_BLE_SM_AUTHEN_REQ_MODE, ESP_BLE_SM_IOCAP_MODE,
    ESP_BLE_SM_MAX_KEY_SIZE, ESP_BLE_SM_SET_INIT_KEY, ESP_BLE_SM_SET_RSP_KEY,
    ESP_GAP_SEARCH_INQ_CMPL_EVT, ESP_GAP_SEARCH_INQ_RES_EVT,
    ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, ESP_BLE_AD_TYPE_NAME_CMPL, ESP_BLE_AD_TYPE_NAME_SHORT
};
constexpr int ESP_UUID_LEN_16 = 2, ESP_UUID_LEN_128 = 16, ESP_BT_STATUS_SUCCESS = 0;
constexpr int ESP_GATT_CHAR_PROP_BIT_WRITE = 8, ESP_GATT_CHAR_PROP_BIT_NOTIFY = 16;
constexpr int ESP_BLE_ENC_KEY_MASK = 1, ESP_BLE_ID_KEY_MASK = 2, ESP_GATT_UUID_CHAR_CLIENT_CONFIG = 0x2902;
struct esp_bt_uuid_t { int len; union { uint16_t uuid16; uint8_t uuid128[16]; } uuid; };
struct esp_ble_scan_params_t { int scan_type, own_addr_type, scan_filter_policy, scan_interval, scan_window, scan_duplicate; };
struct esp_ble_gatt_creat_conn_params_t { uint8_t remote_bda[6]; int remote_addr_type, own_addr_type; bool is_direct; };
struct esp_ble_gattc_cancel_open_params_t { int gattc_if; uint8_t remote_bda[6]; };
struct esp_gattc_char_elem_t { uint16_t char_handle; int properties; };
struct esp_gattc_descr_elem_t { uint16_t handle; };
struct esp_ble_gap_cb_param_t {
    struct { int status; } scan_param_cmpl{}, scan_start_cmpl{};
    struct { int search_evt; uint8_t adv_data_len, scan_rsp_len, ble_adv[62], bda[6]; int ble_addr_type, rssi; } scan_rst{};
    struct {
        struct { uint8_t bd_addr[6]; } ble_req{};
        struct { uint8_t bd_addr[6]; bool success; } auth_cmpl{};
    } ble_security;
};
struct esp_ble_gattc_cb_param_t {
    struct { int status; uint16_t app_id; } reg{};
    struct { uint16_t conn_id; uint8_t remote_bda[6]; } connect{}, disconnect{};
    struct { int status; } open{};
    struct { uint16_t conn_id; struct { esp_bt_uuid_t uuid; } srvc_id; uint16_t start_handle, end_handle; } search_res{};
    struct { uint16_t conn_id; int status; } search_cmpl{};
    struct { uint16_t handle; int status; } reg_for_notify{};
    struct { uint16_t conn_id, handle; int status; } write{};
    struct { uint16_t conn_id, handle; uint8_t* value; uint16_t value_len; } notify{};
};
namespace fake {
inline uint32_t milliseconds = 100;
inline int write_failure = 0, opens = 0, closes = 0, scans = 0, subscriptions = 0, cancellations = 0;
inline std::vector<std::array<uint8_t, 2>> writes;
}
inline int64_t esp_timer_get_time() { return int64_t(fake::milliseconds) * 1000; }
inline const char* esp_err_to_name(int) { return "mock error"; }
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
inline int esp_ble_gap_start_scanning(int) { ++fake::scans; return 0; }
inline int esp_ble_gap_stop_scanning() { return 0; }
inline int esp_ble_gap_set_scan_params(esp_ble_scan_params_t*) { return 0; }
inline int esp_ble_gap_set_security_param(int, void*, size_t) { return 0; }
inline int esp_ble_gap_security_rsp(uint8_t*, bool) { return 0; }
inline int esp_ble_passkey_reply(uint8_t*, bool, int) { return 0; }
inline int esp_ble_set_encryption(uint8_t*, int) { return 0; }
inline int esp_ble_gattc_close(int, uint16_t) { ++fake::closes; return 0; }
inline int esp_ble_gattc_cancel_open(esp_ble_gattc_cancel_open_params_t*) { ++fake::cancellations; return 0; }
inline int esp_ble_gattc_register_for_notify(int, uint8_t*, uint16_t) { return 0; }
inline int esp_ble_gattc_unregister_for_notify(int, uint8_t*, uint16_t) { return 0; }
inline int esp_ble_gattc_register_callback(void (*)(int,int,esp_ble_gattc_cb_param_t*)) { return 0; }
inline int esp_ble_gattc_app_register(uint16_t) { return 0; }
inline int esp_ble_gattc_open(int, uint8_t*, esp_ble_addr_type_t, bool) { ++fake::opens; return 0; }
inline int esp_ble_gattc_search_service(int, uint16_t, esp_bt_uuid_t*) { return 0; }
inline int esp_ble_gattc_get_char_by_uuid(int, uint16_t, uint16_t, uint16_t, esp_bt_uuid_t uuid, esp_gattc_char_elem_t* result, uint16_t* count)
{ *count = 1; result->char_handle = uuid.uuid.uuid16 == 0xff01 ? 12 : 14; result->properties = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY; return 0; }
inline int esp_ble_gattc_get_descr_by_char_handle(int, uint16_t, uint16_t, esp_bt_uuid_t, esp_gattc_descr_elem_t* result, uint16_t* count)
{ *count = 1; result->handle = 15; return 0; }
inline int esp_ble_gattc_write_char_descr(int, uint16_t, uint16_t, uint16_t, uint8_t*, int, int) { ++fake::subscriptions; return 0; }
inline int esp_ble_gattc_write_char(int, uint16_t, uint16_t, uint16_t length, uint8_t* data, int, int)
{ if (length == 2) fake::writes.push_back({data[0], data[1]}); return fake::write_failure; }
inline uint8_t* esp_ble_resolve_adv_data_by_type(uint8_t* data, uint16_t size, int type, uint8_t* length)
{ if (type == ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE && size >= 4) { *length = size; return data; } *length = 0; return nullptr; }
