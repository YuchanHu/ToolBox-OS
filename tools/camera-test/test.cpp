#include "platform.h"
#include <system/camera_remote.h>
#include <system/camera_ble.h>
#include <system/sony_protocol.h>
#include <cstdio>
#include <cstdlib>
using namespace toolbox;
using namespace toolbox::camera;
namespace toolbox {
bool stack_enabled = false;
esp_err_t set_ble_enabled(bool enabled) {
    if (enabled == stack_enabled) return 0;
    stack_enabled = enabled;
    if (enabled) return camera::stack_started();
    camera::stack_stopping();
    return 0;
}
}
void require(bool condition, const char* message) { if (!condition) { puts(message); std::exit(1); } }
void gap(int event, esp_ble_gap_cb_param_t data = {}) { gap_event(event, &data); }
void gatt(int event, esp_ble_gattc_cb_param_t data = {}) { gatt_event(event, 1, &data); }
Device device;
void ready()
{
    start();
    esp_ble_gattc_cb_param_t data{}; data.reg.app_id = 0x42;
    gatt(ESP_GATTC_REG_EVT, data);
    gap(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    tick(); gap(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    connect(device); gap(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT); tick();
    data = {}; data.connect.conn_id = 1;
    std::copy(device.address.begin(), device.address.end(), data.connect.remote_bda);
    gatt(ESP_GATTC_CONNECT_EVT, data);
    data = {}; data.search_res.conn_id = 1; data.search_res.srvc_id.uuid.len = 16;
    std::copy(sony::service_uuid.begin(), sony::service_uuid.end(), data.search_res.srvc_id.uuid.uuid.uuid128);
    data.search_res.start_handle = 10; data.search_res.end_handle = 20;
    gatt(ESP_GATTC_SEARCH_RES_EVT, data);
    data = {}; data.search_cmpl.conn_id = 1; gatt(ESP_GATTC_SEARCH_CMPL_EVT, data);
    data = {}; data.reg_for_notify.handle = 14; gatt(ESP_GATTC_REG_FOR_NOTIFY_EVT, data);
    const int subscriptions = fake::subscriptions; tick();
    require(fake::subscriptions == subscriptions && status().phase != Phase::Ready, "Must wait for encryption before CCCD and control");
    esp_ble_gap_cb_param_t security{};
    std::copy(device.address.begin(), device.address.end(), security.ble_security.auth_cmpl.bd_addr);
    security.ble_security.auth_cmpl.success = true; gap(ESP_GAP_BLE_AUTH_CMPL_EVT, security);
    tick(); require(fake::subscriptions == subscriptions + 1, "Must subscribe after authentication");
    data = {}; data.write.conn_id = 1; data.write.handle = 15; gatt(ESP_GATTC_WRITE_DESCR_EVT, data);
    require(status().phase == Phase::Ready, "Authenticated and subscribed camera must become ready");
}
void write_ack(int status = 0)
{
    esp_ble_gattc_cb_param_t data{}; data.write.conn_id = 1; data.write.handle = 12; data.write.status = status;
    gatt(ESP_GATTC_WRITE_CHAR_EVT, data);
}
void notify(bool recording)
{
    uint8_t bytes[] = {2, 0xd5, uint8_t(recording ? 0x20 : 0)};
    esp_ble_gattc_cb_param_t data{}; data.notify.conn_id = 1; data.notify.handle = 14; data.notify.value = bytes; data.notify.value_len = 3;
    gatt(ESP_GATTC_NOTIFY_EVT, data);
}
void disconnect()
{
    esp_ble_gattc_cb_param_t data{};
    std::copy(device.address.begin(), device.address.end(), data.disconnect.remote_bda);
    gatt(ESP_GATTC_DISCONNECT_EVT, data);
}
int main()
{
    device.address = {1, 2, 3, 4, 5, 6}; device.name = "Sony test";
    const uint8_t prefix[] = {0x2d, 1, 3, 0};
    require(sony::advertisement(prefix, 4) && !sony::advertisement(prefix, 3), "Advertisement length validation");
    bool recording = false; const uint8_t invalid[] = {2, 0xd5, 0x7f};
    require(!sony::recording_notification(invalid, 3, recording), "Unknown notification must not become stopped");
    ready();
    trigger(Mode::Photo); trigger(Mode::Movie);
    for (int step = 0; step < 4; ++step) { tick(); require(fake::writes.back()[1] == sony::photo_sequence[step], "Photo must press and release both half/full shutter in order"); write_ack(); fake::milliseconds += 120; }
    require(!status().busy && !status().recording_known && fake::writes.size() == 4, "Photo must finish once and reject concurrent trigger");
    trigger(Mode::Movie); tick(); require(!status().recording_known, "Recording must await camera feedback");
    notify(true); write_ack(); fake::milliseconds += 80; tick(); write_ack();
    require(fake::writes.back()[1] == 0x0e && !status().busy && status().recording, "Early record notification must not skip button release or leave busy");
    trigger(Mode::Movie); tick(); write_ack(); fake::milliseconds += 80; tick(); write_ack();
    require(status().busy && status().recording, "Record write ACK must not claim movie stopped");
    notify(false); require(!status().busy && !status().recording, "Stop notification must release pending command");
    trigger(Mode::Movie); tick(); write_ack(); fake::milliseconds += 80; tick(); write_ack();
    const auto writes_before_timeout = fake::writes.size(); fake::milliseconds += 5100; tick();
    require(status().phase == Phase::Error && fake::writes.size() == writes_before_timeout, "Timeout must never replay a record toggle");
    disconnect(); fake::milliseconds += 3100; tick();
    require(status().phase == Phase::Scanning, "Disconnected target must scan for reconnection");
    stop(); set_ble_enabled(false); ready();
    fake::write_failure = -1; trigger(Mode::Photo); tick();
    require(status().phase == Phase::Error && !status().busy, "Immediate write error must clear pending command and disconnect");
    fake::write_failure = 0; disconnect(); stop(); set_ble_enabled(false);
    ready(); stop(); disconnect();
    const int previous_opens = fake::opens;
    start(); tick(); gap(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    connect(device); gap(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT); tick();
    require(fake::opens == previous_opens + 1,
            "Reopening app with BLE still enabled must reuse registered client and open again");
    stop(); gatt(ESP_GATTC_CANCEL_OPEN_EVT);
    const auto before = fake::writes.size(); trigger(Mode::Photo); tick();
    require(status().phase == Phase::Off && before == fake::writes.size(), "Closed app must ignore controls and reconnects");
    set_ble_enabled(false); start();
    esp_ble_gattc_cb_param_t registration{}; registration.reg.app_id = 0x42;
    gatt(ESP_GATTC_REG_EVT, registration); gap(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
    tick(); gap(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
    connect(device); gap(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT); tick();
    const int cancellations = fake::cancellations;
    stop();
    require(fake::cancellations == cancellations + 1, "Closing during connection must cancel the pending open");
    gatt(ESP_GATTC_CANCEL_OPEN_EVT); tick();
    require(status().phase == Phase::Off, "Late cancel callback must not restart a closed app");
    set_ble_enabled(false); ready();
    const auto writes_before_power_off = fake::writes.size();
    trigger(Mode::Photo); set_ble_enabled(false); tick();
    require(status().phase == Phase::Off && fake::writes.size() == writes_before_power_off,
            "BLE power-off must cancel queued shutter commands");
    puts("Passed production Sony GATT state machine: authentication, subscription, photo sequence, early/late recording feedback, busy rejection, timeout, reconnect and close");
}
