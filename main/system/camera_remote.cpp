#include "camera_remote.h"
#include "camera_ble.h"
#include "sony_protocol.h"
#include "ble.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <mutex>

namespace toolbox::camera {
namespace {
constexpr uint16_t app_id = 0x42;
constexpr const char* tag = "Sony-Remote";
std::recursive_mutex mutex;
Status current;
Device selected;
esp_gatt_if_t client = ESP_GATT_IF_NONE;
uint16_t connection = 0, service_start = 0, service_end = 0;
uint16_t command_handle = 0, notify_handle = 0, descriptor_handle = 0;
bool active = false, selected_valid = false, scan_configured = false;
bool scanning = false, scan_pending = false, opening = false, linked = false, closing = false;
bool secured = false, subscribed = false, subscribing = false;
bool command_pending = false, sequence_active = false, record_result_received = false;
Mode command_mode = Mode::Photo;
size_t sequence_index = 0;
uint32_t deadline = 0, next_step = 0, retry_at = 0;

uint32_t now() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
bool due(uint32_t time) { return static_cast<int32_t>(now() - time) >= 0; }

void cancel_open()
{
    esp_ble_gattc_cancel_open_params_t parameters = {};
    parameters.gattc_if = client;
    std::copy(selected.address.begin(), selected.address.end(), parameters.remote_bda);
    if (esp_ble_gattc_cancel_open(&parameters) != ESP_OK) opening = false;
}

void clear_link(bool disconnected = false)
{
    if (!disconnected && notify_handle && client != ESP_GATT_IF_NONE)
        esp_ble_gattc_unregister_for_notify(client, selected.address.data(), notify_handle);
    linked = closing = opening = secured = subscribed = subscribing = false;
    service_start = service_end = command_handle = notify_handle = descriptor_handle = 0;
    command_pending = sequence_active = false;
    deadline = next_step = 0;
    current.busy = current.recording_known = current.recording = false;
    current.recording_since = 0;
}

void fail(const char* message)
{
    ESP_LOGW(tag, "%s", message);
    current.phase = Phase::Error;
    current.message = message;
    current.busy = false;
    sequence_active = command_pending = false;
    deadline = 0;
    retry_at = now() + 3000;
    if (opening) cancel_open();
    if (linked && !closing) {
        closing = true;
        esp_ble_gattc_close(client, connection);
    }
}

void check(esp_err_t error, const char* message)
{
    if (error != ESP_OK) {
        ESP_LOGW(tag, "%s: %s", message, esp_err_to_name(error));
        fail(message);
    }
}

void mark_ready()
{
    if (!secured || !subscribed) return;
    current.phase = Phase::Ready;
    current.message = "已连接";
    deadline = 0;
}

void begin_scan()
{
    if (!active || !scan_configured || scanning || scan_pending || opening || linked || closing) return;
    current.phase = Phase::Scanning;
    current.message = selected_valid ? "正在重新连接相机" : "在相机上开启蓝牙遥控配对";
    const esp_err_t result = esp_ble_gap_start_scanning(8);
    scan_pending = result == ESP_OK;
    check(result, "扫描失败");
}

void discover_characteristics()
{
    esp_gattc_char_elem_t element = {};
    uint16_t count = 1;
    esp_bt_uuid_t uuid = {};
    uuid.len = ESP_UUID_LEN_16;
    uuid.uuid.uuid16 = sony::command_uuid;
    if (esp_ble_gattc_get_char_by_uuid(client, connection, service_start, service_end, uuid, &element, &count) != ESP_GATT_OK ||
        count == 0 || !(element.properties & ESP_GATT_CHAR_PROP_BIT_WRITE)) {
        fail("未找到相机控制特征");
        return;
    }
    command_handle = element.char_handle;
    count = 1;
    uuid.uuid.uuid16 = sony::notify_uuid;
    if (esp_ble_gattc_get_char_by_uuid(client, connection, service_start, service_end, uuid, &element, &count) != ESP_GATT_OK ||
        count == 0 || !(element.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) {
        fail("未找到相机状态通知");
        return;
    }
    notify_handle = element.char_handle;
    check(esp_ble_gattc_register_for_notify(client, selected.address.data(), notify_handle), "订阅相机状态失败");
}

void write_step()
{
    const uint8_t code = command_mode == Mode::Photo ? sony::photo_sequence[sequence_index] : sony::movie_sequence[sequence_index];
    uint8_t bytes[] = {0x01, code};
    command_pending = true;
    deadline = now() + 4000;
    ESP_LOGI(tag, "command 01 %02x", code);
    check(esp_ble_gattc_write_char(client, connection, command_handle, sizeof(bytes), bytes,
                                  ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM), "控制指令发送失败");
}
}

Status status() { std::lock_guard<std::recursive_mutex> lock(mutex); return current; }

void start()
{
    // Controller transitions stay on Runtime's main task, outside LVGL locks.
    const esp_err_t result = set_ble_enabled(true);
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (result != ESP_OK) { fail("蓝牙启动失败"); return; }
    active = true;
    selected_valid = false;
    current.devices.clear();
    current.name.clear();
    current.phase = scanning || scan_pending ? Phase::Scanning : Phase::Starting;
    current.message = "在相机上开启蓝牙遥控配对";
    deadline = 0;
    retry_at = 0;
    if (opening) cancel_open();
    if (linked && !closing) {
        closing = true;
        esp_ble_gattc_close(client, connection);
    }
}

void stop()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    active = selected_valid = false;
    current.phase = Phase::Off;
    current.message = "已断开";
    current.busy = false;
    sequence_active = false;
    if (scanning || scan_pending) esp_ble_gap_stop_scanning();
    if (opening) cancel_open();
    if (linked && !closing) {
        closing = true;
        esp_ble_gattc_close(client, connection);
    }
}

void connect(const Device& device)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!active || linked || opening || closing) return;
    selected = device;
    selected_valid = true;
    current.name = device.name;
    current.phase = Phase::Connecting;
    current.message = "正在连接相机";
    deadline = now() + 20000;
    if (scanning || scan_pending) check(esp_ble_gap_stop_scanning(), "停止扫描失败");
}

void trigger(Mode mode)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!active || current.phase != Phase::Ready || !secured || !subscribed || current.busy || closing) return;
    record_result_received = false;
    command_mode = mode;
    sequence_index = 0;
    sequence_active = current.busy = true;
    next_step = now();
    current.message = mode == Mode::Photo ? "正在发送快门指令" : "等待相机录像状态";
}

void tick()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!active) return;
    if (deadline && due(deadline)) { fail("相机响应超时，请检查相机"); return; }
    if (current.phase == Phase::Error) {
        if (!due(retry_at) || linked || opening || closing) return;
        begin_scan();
        return;
    }
    if (current.phase == Phase::Starting) begin_scan();
    if (current.phase == Phase::Connecting && !scanning && !scan_pending && !opening && !linked && !closing && client != ESP_GATT_IF_NONE) {
        opening = true;
        const esp_err_t result = esp_ble_gattc_open(client, selected.address.data(),
            static_cast<esp_ble_addr_type_t>(selected.address_type), true);
        if (result != ESP_OK) opening = false;
        check(result, "相机连接失败");
    }
    if (linked && secured && descriptor_handle && !subscribed && !subscribing && !closing) {
        subscribing = true;
        uint8_t enable[] = {1, 0};
        check(esp_ble_gattc_write_char_descr(client, connection, descriptor_handle, sizeof(enable), enable,
                ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM), "启用相机通知失败");
    }
    if (sequence_active && !command_pending && due(next_step)) write_step();
}

esp_err_t stack_started()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    client = ESP_GATT_IF_NONE;
    scan_configured = false;
    esp_ble_auth_req_t auth = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t capabilities = ESP_IO_CAP_NONE;
    uint8_t size = 16, keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_err_t result = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth));
    if (result == ESP_OK) result = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &capabilities, sizeof(capabilities));
    if (result == ESP_OK) result = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &size, sizeof(size));
    if (result == ESP_OK) result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, sizeof(keys));
    if (result == ESP_OK) result = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, sizeof(keys));
    if (result == ESP_OK) result = esp_ble_gattc_app_register(app_id);
    return result;
}

void stack_stopping()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    active = selected_valid = scanning = scan_pending = scan_configured = false;
    clear_link();
    client = ESP_GATT_IF_NONE;
    current.phase = Phase::Off;
    current.message = "蓝牙已关闭";
}

void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* data)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        scan_configured = data->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (!scan_configured && active) fail("蓝牙扫描配置失败");
        return;
    }
    if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT) {
        scan_pending = false;
        scanning = data->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (!active && scanning) esp_ble_gap_stop_scanning();
        else if (!scanning && active) fail("扫描失败");
        return;
    }
    if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) { scanning = scan_pending = false; return; }
    if (!active) return;
    if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        auto& scan = data->scan_rst;
        if (scan.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            scanning = scan_pending = false;
            if (current.phase == Phase::Scanning) begin_scan();
            return;
        }
        if (scan.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT || current.phase != Phase::Scanning) return;
        const uint8_t length = scan.adv_data_len + scan.scan_rsp_len;
        uint8_t manufacturer_length = 0, name_length = 0;
        auto* manufacturer = esp_ble_resolve_adv_data_by_type(scan.ble_adv, length,
            ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &manufacturer_length);
        if (!manufacturer || !sony::advertisement(manufacturer, manufacturer_length)) return;
        Device device;
        std::copy(scan.bda, scan.bda + 6, device.address.begin());
        device.address_type = scan.ble_addr_type;
        device.rssi = scan.rssi;
        auto* name = esp_ble_resolve_adv_data_by_type(scan.ble_adv, length, ESP_BLE_AD_TYPE_NAME_CMPL, &name_length);
        if (!name) name = esp_ble_resolve_adv_data_by_type(scan.ble_adv, length, ESP_BLE_AD_TYPE_NAME_SHORT, &name_length);
        char fallback[32];
        snprintf(fallback, sizeof(fallback), "Sony %02X:%02X:%02X", scan.bda[3], scan.bda[4], scan.bda[5]);
        device.name = name ? std::string(reinterpret_cast<char*>(name), std::min<int>(name_length, 28)) : fallback;
        if (selected_valid) {
            if (device.address == selected.address) connect(device);
            return;
        }
        auto found = std::find_if(current.devices.begin(), current.devices.end(), [&](const Device& item) { return item.address == device.address; });
        if (found != current.devices.end()) *found = device;
        else if (current.devices.size() < 8) current.devices.push_back(device);
        return;
    }
    if (event != ESP_GAP_BLE_SEC_REQ_EVT && event != ESP_GAP_BLE_AUTH_CMPL_EVT && event != ESP_GAP_BLE_PASSKEY_REQ_EVT) return;
    const uint8_t* peer = event == ESP_GAP_BLE_AUTH_CMPL_EVT
        ? data->ble_security.auth_cmpl.bd_addr : data->ble_security.ble_req.bd_addr;
    if (!linked || std::memcmp(selected.address.data(), peer, 6) != 0) return;
    switch (event) {
    case ESP_GAP_BLE_SEC_REQ_EVT:
        check(esp_ble_gap_security_rsp(selected.address.data(), true), "配对确认失败");
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (!data->ble_security.auth_cmpl.success) fail("配对失败，请在相机上重新配对");
        else { secured = true; mark_ready(); }
        break;
    case ESP_GAP_BLE_PASSKEY_REQ_EVT:
        esp_ble_passkey_reply(selected.address.data(), false, 0);
        fail("相机需要配对码，请重新配对");
        break;
    default: break;
    }
}

void gatt_event(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t* data)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (event == ESP_GATTC_REG_EVT) {
        if (data->reg.app_id != app_id) return;
        if (data->reg.status != ESP_GATT_OK) { fail("相机服务注册失败"); return; }
        client = interface;
        esp_ble_scan_params_t parameters = {};
        parameters.scan_type = BLE_SCAN_TYPE_ACTIVE;
        parameters.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        parameters.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
        parameters.scan_interval = 0x80;
        parameters.scan_window = 0x40;
        parameters.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
        check(esp_ble_gap_set_scan_params(&parameters), "扫描参数配置失败");
        return;
    }
    if (interface != client) return;
    if (event == ESP_GATTC_CANCEL_OPEN_EVT) {
        if (!linked) opening = false;
        return;
    }
    if (event == ESP_GATTC_CONNECT_EVT) {
        if (!opening && !active) return;
        if (!active || !selected_valid || closing || current.phase == Phase::Error || std::memcmp(data->connect.remote_bda, selected.address.data(), 6) != 0) {
            connection = data->connect.conn_id;
            opening = false;
            linked = closing = true;
            esp_ble_gattc_close(interface, data->connect.conn_id);
            return;
        }
        linked = true;
        opening = false;
        connection = data->connect.conn_id;
        current.phase = Phase::Pairing;
        current.message = "请在相机上确认蓝牙配对";
        deadline = now() + 45000;
        check(esp_ble_set_encryption(selected.address.data(), ESP_BLE_SEC_ENCRYPT_NO_MITM), "配对启动失败");
        esp_bt_uuid_t uuid = {};
        uuid.len = ESP_UUID_LEN_128;
        std::copy(sony::service_uuid.begin(), sony::service_uuid.end(), uuid.uuid.uuid128);
        check(esp_ble_gattc_search_service(client, connection, &uuid), "发现相机服务失败");
        return;
    }
    if (event == ESP_GATTC_OPEN_EVT) {
        if (data->open.status != ESP_GATT_OK) { opening = false; if (active) fail("连接失败，正在等待重连"); }
        return;
    }
    if (event == ESP_GATTC_DISCONNECT_EVT) {
        if (std::memcmp(data->disconnect.remote_bda, selected.address.data(), 6) != 0) return;
        // Bluedroid already removes notification registrations on disconnect.
        clear_link(true);
        if (active && current.phase != Phase::Error) {
            current.phase = Phase::Starting;
            current.message = "连接断开，正在重新连接";
        }
        return;
    }
    if (!active || !linked || closing) return;
    switch (event) {
    case ESP_GATTC_SEARCH_RES_EVT:
        if (data->search_res.conn_id == connection && data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128 &&
            std::memcmp(data->search_res.srvc_id.uuid.uuid.uuid128, sony::service_uuid.data(), 16) == 0) {
            service_start = data->search_res.start_handle;
            service_end = data->search_res.end_handle;
        }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (data->search_cmpl.conn_id != connection) break;
        if (data->search_cmpl.status != ESP_GATT_OK || !service_start) fail("相机不支持 Sony 蓝牙遥控");
        else discover_characteristics();
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (data->reg_for_notify.handle != notify_handle) break;
        if (data->reg_for_notify.status != ESP_GATT_OK) { fail("相机通知注册失败"); break; }
        esp_gattc_descr_elem_t descriptor = {};
        esp_bt_uuid_t uuid = {};
        uuid.len = ESP_UUID_LEN_16;
        uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        uint16_t count = 1;
        if (esp_ble_gattc_get_descr_by_char_handle(client, connection, notify_handle, uuid, &descriptor, &count) != ESP_GATT_OK || !count)
            fail("未找到相机通知描述符");
        else descriptor_handle = descriptor.handle;
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (data->write.conn_id != connection || data->write.handle != descriptor_handle) break;
        if (data->write.status != ESP_GATT_OK) fail("订阅相机通知失败");
        else { subscribed = true; mark_ready(); }
        break;
    case ESP_GATTC_NOTIFY_EVT: {
        if (data->notify.conn_id != connection || data->notify.handle != notify_handle) break;
        bool recording = false;
        if (sony::recording_notification(data->notify.value, data->notify.value_len, recording)) {
            if (recording && (!current.recording_known || !current.recording)) current.recording_since = now();
            if (current.busy && command_mode == Mode::Movie) record_result_received = true;
            current.recording = recording;
            current.recording_known = true;
            if (!sequence_active && command_mode == Mode::Movie) { current.busy = false; deadline = 0; }
            current.message = recording ? "相机正在录像" : "相机已停止录像";
            ESP_LOGI(tag, "camera recording=%d", recording);
        }
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (data->write.conn_id != connection || data->write.handle != command_handle || !command_pending) break;
        command_pending = false;
        if (data->write.status != ESP_GATT_OK) { fail("相机拒绝控制指令，请重新连接"); break; }
        ++sequence_index;
        if (sequence_index < (command_mode == Mode::Photo ? sony::photo_sequence.size() : sony::movie_sequence.size())) {
            next_step = now() + (command_mode == Mode::Photo ? 120 : 80);
        } else {
            sequence_active = false;
            if (command_mode == Mode::Photo) {
                current.busy = false;
                current.message = "快门指令已发送";
                deadline = 0;
            } else if (record_result_received) {
                current.busy = false;
                current.message = current.recording ? "相机正在录像" : "相机已停止录像";
                deadline = 0;
            } else {
                // Never replay a toggle automatically after an uncertain result.
                current.message = "等待相机录像状态";
                deadline = now() + 5000;
            }
        }
        break;
    default: break;
    }
}
}
