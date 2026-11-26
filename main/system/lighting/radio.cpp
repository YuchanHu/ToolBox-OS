#include "radio.h"
#include "mesh.h"
#include "radio_ble.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <esp_gatt_common_api.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mutex>
#include <system/ble.h>

namespace toolbox::lighting {
namespace {
constexpr uint16_t app_id = 0x43;
constexpr const char *tag = "Lighting";
std::recursive_mutex mutex;
RadioState state;
Brand brand = Brand::Inkee;
bool active = false, scan_pending = false, scanning = false, scan_ready = false;
bool opening = false, linked = false, closing = false, writing = false;
esp_gatt_if_t client = ESP_GATT_IF_NONE;
uint16_t conn = 0, service_start = 0, service_end = 0, write_handle = 0, notify_handle = 0, desc_handle = 0;
uint16_t alternate_start = 0, alternate_end = 0, handshake_start = 0, handshake_end = 0, handshake_handle = 0;
uint8_t cccd_mode = 1;
bool gatt_ready = false;
esp_gatt_write_type_t write_type = ESP_GATT_WRITE_TYPE_NO_RSP;
uint32_t deadline = 0, next_send = 0, scan_finish = 0;
uint16_t zhiyun_sequence = 0;
uint8_t sequence = 0, roll = 0;
XsjSession xsj;
std::deque<Bytes> writes;
Bytes notification_buffer;
State previous, pending_state;
bool previous_valid = false, query_wait = false, pending_send = false;
struct Burst {
    uint16_t company;
    Bytes data;
    uint32_t duration;
};
std::deque<Burst> bursts;
enum class Adv { Idle, StoppingIdle, Configuring, Starting, Sending, Stopping };
Adv adv = Adv::Idle;
uint32_t adv_until = 0;
bool idle_stopped = false;
uint32_t now() { return uint32_t(esp_timer_get_time() / 1000); }
bool due(uint32_t t) { return int32_t(now() - t) >= 0; }
void message(Phase p, const char *text) {
    state.phase = p;
    state.message = text;
}
void fail(const char *text) {
    ESP_LOGW(tag, "%s", text);
    message(Phase::Error, text);
    deadline = 0;
    writes.clear();
    bursts.clear();
    pending_send = false;
    writing = false;
    query_wait = false;
    previous_valid = false;
}
void end_link() {
    if (opening && client != ESP_GATT_IF_NONE) {
        esp_ble_gattc_cancel_open_params_t p = {};
        p.gattc_if = client;
        std::copy(state.selected.address.begin(), state.selected.address.end(), p.remote_bda);
        esp_ble_gattc_cancel_open(&p);
    }
    if (linked && !closing) {
        closing = true;
        esp_ble_gattc_close(client, conn);
    }
}
void clear_link() {
    opening = linked = closing = writing = false;
    write_handle = notify_handle = desc_handle = 0;
    service_start = service_end = 0;
    alternate_start = alternate_end = handshake_start = handshake_end = handshake_handle = 0;
    gatt_ready = false;
    writes.clear();
    notification_buffer.clear();
    deadline = 0;
    query_wait = false;
    previous_valid = false;
}
esp_bt_uuid_t uuid16(uint16_t u) {
    esp_bt_uuid_t a{};
    a.len = ESP_UUID_LEN_16;
    a.uuid.uuid16 = u;
    return a;
}
esp_bt_uuid_t uuid128(const std::array<uint8_t, 16> &big) {
    esp_bt_uuid_t a{};
    a.len = ESP_UUID_LEN_128;
    std::reverse_copy(big.begin(), big.end(), a.uuid.uuid128);
    return a;
}
esp_bt_uuid_t yn_uuid(uint8_t short_id) {
    return uuid128({0xf0, 0, 0xaa, short_id, 4, 0x51, 0x40, 0, 0xb0, 0, 0, 0, 0, 0, 0, 0});
}
esp_bt_uuid_t ulanzi_handshake_uuid(uint8_t short_id) {
    return uuid128({0xf0, 0, 0xff, short_id, 4, 0x51, 0x40, 0, 0xb0, 0, 0, 0, 0, 0, 0, 0});
}
esp_bt_uuid_t nan_uuid(uint8_t short_id) {
    return uuid128({0, 3, 0xcd, short_id, 0, 0, 0x10, 0, 0x80, 0, 0, 0x80, 0x5f, 0x9b, 1, 0x31});
}
esp_bt_uuid_t zy_uuid(uint8_t last) {
    return uuid128(
        {0xd4, 0x4b, 0xc4, 0x39, 0xab, 0xfd, 0x45, 0xa2, 0xb5, 0x75, 0x92, 0x54, 0x16, 0x12, 0x96, last});
}
struct Profile {
    esp_bt_uuid_t service, write, notify, alternate;
    bool requires_notify;
    int interval;
};
Profile profile() {
    switch (brand) {
    case Brand::Yongnuo:
        return {yn_uuid(0x60), yn_uuid(0x61), yn_uuid(0x63), {}, true, 55};
    case Brand::Nanlite:
        return {nan_uuid(0xd0), nan_uuid(0xd2), nan_uuid(0xd1), {}, true, 30};
    case Brand::Xsj:
        return {uuid16(0xae30), uuid16(0xae01), uuid16(0xae02), uuid16(0xae00), false, 35};
    case Brand::Ulanzi:
        return {uuid16(0xfff0), uuid16(0xfff2), uuid16(0xfff1), {}, true, 60};
    case Brand::Zhiyun:
        return {uuid16(0xfee9), zy_uuid(0), zy_uuid(1), {}, true, 60};
    default:
        return {};
    }
}
bool same_uuid(const esp_bt_uuid_t &a, const esp_bt_uuid_t &b) {
    return a.len == b.len && (a.len == ESP_UUID_LEN_16 ? a.uuid.uuid16 == b.uuid.uuid16
                                                       : a.len == ESP_UUID_LEN_128 &&
                                                             memcmp(a.uuid.uuid128, b.uuid.uuid128, 16) == 0);
}
std::string upper(const char *name) {
    std::string s = name;
    for (char &c : s)
        c = std::toupper((unsigned char)c);
    return s;
}
void enqueue(Frames frames) {
    for (auto &f : frames)
        if (!f.empty())
            writes.push_back(std::move(f));
}
void scan_result(esp_ble_gap_cb_param_t *p) {
    auto &r = p->scan_rst;
    if (r.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT)
        return;
    uint8_t len = r.adv_data_len + r.scan_rsp_len, n = 0;
    auto *name = esp_ble_resolve_adv_data_by_type(r.ble_adv, len, ESP_BLE_AD_TYPE_NAME_CMPL, &n);
    if (!name)
        name = esp_ble_resolve_adv_data_by_type(r.ble_adv, len, ESP_BLE_AD_TYPE_NAME_SHORT, &n);
    if (!name || !n)
        return;
    Device d;
    d.brand = brand;
    d.address_type = r.ble_addr_type;
    d.rssi = r.rssi;
    std::copy(r.bda, r.bda + 6, d.address.begin());
    memcpy(d.name, name, std::min<size_t>(n, sizeof(d.name) - 1));
    const auto nm = upper(d.name);
    bool match = false;
    uint8_t mlen = 0;
    auto *m = esp_ble_resolve_adv_data_by_type(r.ble_adv, len, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mlen);
    switch (brand) {
    case Brand::Yongnuo:
        match = nm.rfind("YN", 0) == 0 || nm.find("YONGNUO") != std::string::npos ||
                nm.find("P360") != std::string::npos;
        d.variant = yn_variant(nm);
        d.caps = yn_capability(nm, 0, d.variant);
        if (d.variant == YnVariant::Classic360)
            d.channel = 1;
        break;
    case Brand::Xsj:
        match = nm.rfind("GFMESH", 0) == 0;
        break;
    case Brand::Nanlite: {
        int ch = 0;
        char code[7] = {};
        match = sscanf(d.name, "``NL %d %6[0-9A-Fa-f]", &ch, code) == 2 && ch >= 0 && ch <= 0x7fff;
        for (char &c : code)
            c = std::toupper((unsigned char)c);
        auto *p = nanlite_profile(code);
        match = match && p;
        if (match) {
            strcpy(d.product, code);
            d.channel = ch;
            d.caps = p->caps;
        }
        break;
    }
    case Brand::Ulanzi:
        if (nm.find("ULANZI") != std::string::npos && m && mlen >= 7 && m[0] >= 9 && m[5] == 13) {
            snprintf(d.product, sizeof(d.product), "0D&%02X", m[6]);
            if (auto *p = find_product(brand, d.product)) {
                d.caps = p->caps;
                match = true;
                snprintf(d.name, sizeof(d.name), "%s", p->name);
            }
        }
        break;
    case Brand::Zhiyun: {
        const char *code = nullptr;
        if (m && mlen >= 7 && m[0] == 5 && m[1] == 9) {
            if (memcmp(m + 2, "PL103", 5) == 0)
                code = "PL103";
            if (memcmp(m + 2, "PL105", 5) == 0)
                code = "PL105";
        }
        if (!code) {
            if (nm.rfind("PL103", 0) == 0 || nm.find("MOLUS G60") != std::string::npos)
                code = "PL103";
            if (nm.rfind("PL105", 0) == 0 || nm.find("MOLUS X100") != std::string::npos)
                code = "PL105";
        }
        if (code) {
            auto *p = find_product(brand, code);
            strcpy(d.product, code);
            d.caps = p->caps;
            snprintf(d.name, sizeof(d.name), "%s", p->name);
            match = true;
        }
        break;
    }
    default:
        break;
    }
    if (!match)
        return;
    auto it = std::find_if(state.devices.begin(), state.devices.end(),
                           [&](const Device &old) { return old.address == d.address; });
    if (it != state.devices.end())
        *it = d;
    else if (state.devices.size() < 16)
        state.devices.push_back(d);
}
void begin_scan() {
    if (scan_pending || scanning || !scan_ready || client == ESP_GATT_IF_NONE)
        return;
    scan_pending = esp_ble_gap_start_scanning(10) == ESP_OK;
    if (scan_pending) {
        message(Phase::Scanning, "正在扫描");
        scan_finish = now() + 10500;
    } else
        fail("扫描失败");
}
void configure_scan() {
    esp_ble_scan_params_t p = {};
    p.scan_type = BLE_SCAN_TYPE_ACTIVE;
    p.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    p.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    p.scan_interval = 0x80;
    p.scan_window = 0x40;
    p.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    scan_ready = false;
    if (esp_ble_gap_set_scan_params(&p) != ESP_OK)
        fail("扫描配置失败");
}
void start_query() {
    Device &d = state.selected;
    gatt_ready = true;
    query_wait = false;
    if (brand == Brand::Yongnuo) {
        enqueue(yn_query());
        query_wait = true;
    } else if (brand == Brand::Xsj) {
        xsj = {d.address};
        enqueue(xsj_join(xsj));
    } else if (brand == Brand::Nanlite) {
        roll = 0;
        enqueue(nanlite_usr_join(roll, now()));
    } else if (brand == Brand::Ulanzi) {
        enqueue({ulanzi_query()});
        query_wait = true;
    } else if (brand == Brand::Zhiyun) {
        zhiyun_sequence = 0;
        enqueue({zhiyun_query_info(++zhiyun_sequence), zhiyun_query_id(++zhiyun_sequence)});
        query_wait = true;
    }
    next_send = now();
    deadline = now() + 6000;
    message(Phase::Connecting, "读取灯具信息");
}
void on_notify(const uint8_t *d, size_t n) {
    auto &device = state.selected;
    if (brand == Brand::Yongnuo) {
        auto result = yn_status(d, n);
        if (result.channel) {
            device.channel = result.channel;
            auto v = yn_variant(device.name, result.product);
            if (result.legacy && v == YnVariant::Modern)
                v = YnVariant::Legacy;
            device.variant = v;
            device.caps = yn_capability(device.name, result.product, v);
            state.channel_known = true;
            query_wait = false;
        }
    } else if (brand == Brand::Ulanzi) {
        notification_buffer.insert(notification_buffer.end(), d, d + n);
        auto &b = notification_buffer;
        while (b.size() >= 3) {
            if (b[0] != 0xff || b[1] != 0xb1 || b[2] < 19) {
                b.erase(b.begin());
                continue;
            }
            size_t length = b[2];
            if (b.size() < length)
                break;
            if (b[11] > 1) {
                b.erase(b.begin());
                continue;
            }
            auto word = [&](size_t i) { return uint16_t(b[i] * 256 + b[i + 1]); };
            uint16_t min = word(12), max = word(14);
            if (min >= 1000 && max >= min && max <= 20000) {
                device.caps.min_kelvin = min;
                device.caps.max_kelvin = max;
            }
            if (b[16] == 1) {
                device.caps.min_tint = -10;
                device.caps.max_tint = 10;
            } else {
                device.caps.min_tint = device.caps.max_tint = 0;
            }
            query_wait = false;
            b.erase(b.begin(), b.begin() + length);
        }
    } else if (brand == Brand::Zhiyun) {
        notification_buffer.insert(notification_buffer.end(), d, d + n);
        auto &b = notification_buffer;
        while (b.size() >= 4) {
            if (b[0] != 0x24 || b[1] != 0x3c) {
                b.erase(b.begin());
                continue;
            }
            size_t body = b[2] + 256 * b[3];
            if (body < 6 || body > 1024) {
                b.erase(b.begin());
                continue;
            }
            size_t total = body + 6;
            if (b.size() < total)
                break;
            unsigned crc = 0;
            for (size_t i = 4; i < 4 + body; ++i) {
                crc ^= unsigned(b[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = ((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0)) & 0xffff;
            }
            if (crc == (b[4 + body] | (b[5 + body] << 8)) && b[4] == 1 && b[5] == 0 && b[8] == 5 &&
                b[9] == 0x20 && body == 8) {
                device.target = uint16_t(b[10] | (b[11] << 8));
                if (device.target != 0xffff)
                    query_wait = false;
            }
            b.erase(b.begin(), b.begin() + total);
        }
    }
}
void subscribe() {
    const auto p = profile();
    esp_gattc_char_elem_t ch{};
    uint16_t count = 1;
    if (esp_ble_gattc_get_char_by_uuid(client, conn, service_start, service_end, p.notify, &ch, &count) ==
            ESP_GATT_OK &&
        count && (ch.properties & (ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_INDICATE))) {
        notify_handle = ch.char_handle;
        cccd_mode = ch.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY ? 1 : 2;
        if (esp_ble_gattc_register_for_notify(client, state.selected.address.data(), notify_handle) == ESP_OK)
            return;
    }
    if (p.requires_notify) {
        fail("无法订阅灯具状态");
        end_link();
    } else
        start_query();
}
bool use_write_response(uint8_t properties, bool prefer_response) {
    return (properties & ESP_GATT_CHAR_PROP_BIT_WRITE) &&
           (prefer_response || !(properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR));
}
void prepare_notifications() {
    if (brand != Brand::Ulanzi) {
        subscribe();
        return;
    }
    esp_gattc_char_elem_t ch{};
    uint16_t count = 1;
    if (handshake_start && esp_ble_gattc_get_char_by_uuid(client, conn, handshake_start, handshake_end,
                                   ulanzi_handshake_uuid(0xc1), &ch, &count) == ESP_GATT_OK &&
        count && (ch.properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR))) {
        handshake_handle = ch.char_handle;
        uint8_t command = 9;
        auto type = use_write_response(ch.properties, true) ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP;
        if (esp_ble_gattc_write_char(client, conn, handshake_handle, 1, &command, type, ESP_GATT_AUTH_REQ_NONE) ==
            ESP_OK)
            return;
    }
    fail("灯具握手失败");
    end_link();
}
void discover() {
    const auto p = profile();
    esp_gattc_char_elem_t ch{};
    auto find_writer = [&](uint16_t first, uint16_t last) {
        uint16_t count = 1;
        return first && esp_ble_gattc_get_char_by_uuid(client, conn, first, last, p.write, &ch, &count) ==
                            ESP_GATT_OK &&
               count;
    };
    if (!find_writer(service_start, service_end)) {
        service_start = alternate_start;
        service_end = alternate_end;
        if (!find_writer(service_start, service_end)) {
            fail("未找到控灯服务");
            end_link();
            return;
        }
    }
    const bool prefer_response = brand == Brand::Nanlite || brand == Brand::Ulanzi || brand == Brand::Zhiyun;
    const bool with_response = use_write_response(ch.properties, prefer_response);
    if (!with_response && !(ch.properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) {
        fail("控灯服务不可写");
        end_link();
        return;
    }
    write_handle = ch.char_handle;
    write_type = with_response ? ESP_GATT_WRITE_TYPE_RSP : ESP_GATT_WRITE_TYPE_NO_RSP;
    if (brand == Brand::Xsj) {
        if (esp_ble_gatt_set_local_mtu(128) == ESP_OK && esp_ble_gattc_send_mtu_req(client, conn) == ESP_OK)
            return;
        fail("灯具 MTU 协商失败");
        end_link();
        return;
    }
    prepare_notifications();
}
void broadcast_start() {
    if (bursts.empty() || adv != Adv::Idle || !idle_stopped)
        return;
    auto &b = bursts.front();
    Bytes raw{2, 1, 6, uint8_t(b.data.size() + 3), 0xff, uint8_t(b.company), uint8_t(b.company >> 8)};
    raw.insert(raw.end(), b.data.begin(), b.data.end());
    adv = Adv::Configuring;
    if (esp_ble_gap_config_adv_data_raw(raw.data(), raw.size()) != ESP_OK) {
        adv = Adv::Idle;
        fail("广播配置失败");
    }
}
void enqueue_burst(uint16_t company, Bytes data, uint32_t duration) {
    if (!data.empty())
        bursts.push_back({company, std::move(data), duration});
}
void emit_state(const State &s) {
    auto &d = state.selected;
    const State *p = previous_valid ? &previous : nullptr;
    if (brand == Brand::Inkee) {
        InkeeAddress a{d.controller, 1, d.group};
        if (!p || s.power != p->power)
            enqueue_burst(0x004e, inkee_frame(a, InkeeCommand::Power, s.power, sequence++), 250);
        if (s.power) {
            if (!p || !p->power || s.brightness != p->brightness)
                enqueue_burst(0x004e,
                              inkee_frame(a, InkeeCommand::Brightness, (s.brightness + 5) / 10, sequence++),
                              250);
            if (!p || !p->power || s.kelvin != p->kelvin)
                enqueue_burst(0x004e, inkee_frame(a, InkeeCommand::Temperature, s.kelvin, sequence++), 250);
        }
    } else if (brand == Brand::Lyd)
        enqueue_burst(0xffff, lyd_frame(d.channel, d.group, s), 250);
    else if (brand == Brand::Yongnuo)
        enqueue(yn_control(d.variant, d.channel, d.caps, s, p));
    else if (brand == Brand::Xsj)
        enqueue(xsj_control(xsj, s));
    else if (brand == Brand::Nanlite) {
        if (auto *n = nanlite_profile(d.product))
            enqueue(nanlite_usr_wrap(roll, nanlite_payload(*n, d.channel, s)));
    } else if (brand == Brand::Ulanzi)
        enqueue(ulanzi_control(d.caps, s, p));
    else if (brand == Brand::Zhiyun)
        enqueue(zhiyun_control(d.target, zhiyun_sequence, s, p));
    previous = s;
    previous_valid = true;
    if (!bursts.empty() || !writes.empty()) {
        next_send = now();
        deadline = now() + 10000;
        message(Phase::Sending, "正在发送");
    }
}
} // namespace
const char *brand_name(Brand b) {
    switch (b) {
    case Brand::Inkee:
        return "影器";
    case Brand::Yongnuo:
        return "永诺";
    case Brand::Sidus:
        return "爱图仕 / amaran";
    case Brand::Lyd:
        return "LYD / 轻装时代";
    case Brand::Godox:
        return "神牛";
    case Brand::Lumin:
        return "印迹";
    case Brand::Jinbei:
        return "金贝";
    case Brand::Xsj:
        return "新摄角";
    case Brand::Nanlite:
        return "南光";
    case Brand::Ulanzi:
        return "Ulanzi";
    case Brand::Zhiyun:
        return "智云";
    }
    return "";
}
bool is_broadcast(Brand b) { return b == Brand::Inkee || b == Brand::Lyd; }
bool is_mesh(Brand b) {
    return b == Brand::Sidus || b == Brand::Godox || b == Brand::Lumin || b == Brand::Jinbei;
}
bool owns_radio() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    return active;
}
esp_err_t stack_started() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    client = ESP_GATT_IF_NONE;
    return esp_ble_gattc_app_register(app_id);
}
void stack_stopping() {
    stop();
    std::lock_guard<std::recursive_mutex> l(mutex);
    active = false;
    clear_link();
    client = ESP_GATT_IF_NONE;
    scan_pending = scanning = scan_ready = false;
    bursts.clear();
    adv = Adv::Idle;
    idle_stopped = false;
    state = {};
}
void start(Brand b) {
    stop();
    if (set_ble_enabled(true) != ESP_OK) {
        std::lock_guard<std::recursive_mutex> l(mutex);
        fail("蓝牙启动失败");
        return;
    }
    std::lock_guard<std::recursive_mutex> l(mutex);
    brand = b;
    state = {};
    active = true;
    previous_valid = false;
    sequence = esp_random();
    scan_ready = false;
    if (is_mesh(b)) {
        message(Phase::Starting, "启动 Mesh");
        mesh::start(b);
        return;
    }
    if (is_broadcast(b)) {
        idle_stopped = false;
        adv = Adv::StoppingIdle;
        auto r = esp_ble_gap_stop_advertising();
        if (r != ESP_OK) {
            adv = Adv::Idle;
            idle_stopped = true;
        }
        message(Phase::Ready, "广播控制");
        return;
    }
    message(Phase::Starting, "启动扫描");
    configure_scan();
    deadline = now() + 15000;
}
void stop() {
    bool was_mesh = false, was_broadcast = false;
    {
        std::lock_guard<std::recursive_mutex> l(mutex);
        if (!active)
            return;
        was_mesh = is_mesh(brand);
        was_broadcast = is_broadcast(brand);
        active = false;
        pending_send = false;
        deadline = 0;
        writes.clear();
        bursts.clear();
        if (scanning || scan_pending)
            esp_ble_gap_stop_scanning();
        scanning = scan_pending = false;
        end_link();
        state = {};
        if (was_broadcast) {
            esp_ble_gap_stop_advertising();
            adv = Adv::Idle;
            idle_stopped = false;
        }
    }
    if (was_mesh)
        mesh::stop();
    if (was_broadcast || was_mesh)
        resume_idle_ble_advertising();
}
RadioState status() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (active && is_mesh(brand))
        return mesh::status();
    return state;
}
void connect(const Device &d) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active || d.brand != brand)
        return;
    if (is_mesh(brand)) {
        mesh::connect(d);
        return;
    }
    if (opening || linked || closing)
        return;
    state.selected = d;
    state.selected_valid = true;
    state.channel_known = d.channel != 0;
    previous_valid = false;
    if (is_broadcast(brand)) {
        message(Phase::Ready, "广播控制");
        return;
    }
    if (scanning || scan_pending)
        esp_ble_gap_stop_scanning();
    message(Phase::Connecting, "正在连接");
    deadline = now() + 20000;
}
void set_channel(uint16_t ch) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (brand == Brand::Yongnuo && ch >= 1 && ch <= 255) {
        state.selected.channel = ch;
        state.channel_known = true;
        previous_valid = false;
    }
}
void pair_inkee() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active || brand != Brand::Inkee || !state.selected_valid || !bursts.empty() || adv != Adv::Idle)
        return;
    auto &d = state.selected;
    enqueue_burst(0x004e, inkee_frame({d.controller, 1, d.group}, InkeeCommand::Pair, 0, sequence++), 500);
    deadline = now() + 4000;
    message(Phase::Sending, "发送配对广播");
}
void send(const State &s, bool force) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active)
        return;
    if (is_mesh(brand)) {
        mesh::send(s, force);
        return;
    }
    if (!state.selected_valid || (state.phase != Phase::Ready && state.phase != Phase::Sending))
        return;
    if (brand == Brand::Yongnuo && !state.selected.channel) {
        state.message = "请先设置灯具 CH";
        return;
    }
    if (brand == Brand::Zhiyun && state.selected.target == 0xffff) {
        state.message = "未读取控制地址";
        return;
    }
    if (force) previous_valid = false;
    pending_state = s;
    pending_send = true;
    if (state.phase == Phase::Ready) {
        pending_send = false;
        emit_state(s);
    }
}
void tick() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active)
        return;
    if (!ble_enabled()) {
        fail("蓝牙已关闭");
        return;
    }
    if (is_mesh(brand)) {
        mesh::tick();
        return;
    }
    if (deadline && due(deadline) && state.phase != Phase::Ready) {
        if (query_wait && writes.empty() && !writing) {
            query_wait = false;
            if (brand == Brand::Zhiyun && state.selected.target == 0xffff &&
                strcmp(state.selected.product, "PL105") == 0)
                state.selected.target = 0x8003;
            message(Phase::Ready,
                    brand == Brand::Yongnuo && !state.selected.channel ? "请设置 CH" : "已连接");
            deadline = 0;
        } else {
            fail("操作超时");
            end_link();
            if (is_broadcast(brand))
                esp_ble_gap_stop_advertising();
        }
        return;
    }
    if (is_broadcast(brand)) {
        if (adv == Adv::Sending && due(adv_until)) {
            adv = Adv::Stopping;
            esp_ble_gap_stop_advertising();
        }
        if (adv == Adv::Idle) {
            if (!bursts.empty())
                broadcast_start();
            else if (state.phase == Phase::Sending) {
                deadline = 0;
                message(Phase::Ready, "广播已发送");
            }
        }
    } else {
        if (state.phase == Phase::Starting)
            begin_scan();
        if (state.phase == Phase::Scanning && scan_finish && due(scan_finish)) {
            if (scanning)
                esp_ble_gap_stop_scanning();
            state.message = state.devices.empty() ? "未发现灯具" : "选择灯具";
            deadline = 0;
            scan_finish = 0;
        }
        if (state.phase == Phase::Connecting && !linked && !opening && !closing && !scan_pending &&
            !scanning && state.selected_valid && client != ESP_GATT_IF_NONE) {
            opening = true;
            auto &d = state.selected;
            if (esp_ble_gattc_open(client, d.address.data(), esp_ble_addr_type_t(d.address_type), true) !=
                ESP_OK) {
                opening = false;
                fail("连接失败");
            }
        }
        if (linked && write_handle && !writing && !writes.empty() && due(next_send)) {
            auto &b = writes.front();
            writing = true;
            if (esp_ble_gattc_write_char(client, conn, write_handle, b.size(), b.data(), write_type,
                                         ESP_GATT_AUTH_REQ_NONE) != ESP_OK) {
                writing = false;
                fail("写入失败");
                end_link();
            }
        }
        if (linked && gatt_ready && writes.empty() && !writing && !query_wait &&
            (state.phase == Phase::Connecting || state.phase == Phase::Sending)) {
            message(Phase::Ready, state.phase == Phase::Sending ? "指令已发送" : "已连接");
            deadline = 0;
        }
    }
    if (state.phase == Phase::Ready && pending_send) {
        pending_send = false;
        emit_state(pending_state);
    }
}
void gap_event(esp_gap_ble_cb_event_t e, esp_ble_gap_cb_param_t *p) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active || is_mesh(brand))
        return;
    if (e == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        scan_ready = p->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (!scan_ready)
            fail("扫描配置失败");
    } else if (e == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT) {
        scan_pending = false;
        scanning = p->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
        if (!scanning)
            fail("扫描失败");
    } else if (e == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) {
        scanning = scan_pending = false;
    } else if (e == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if (p->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            scanning = scan_pending = false;
            scan_finish = 0;
            deadline = 0;
            if (state.phase == Phase::Scanning)
                state.message = state.devices.empty() ? "未发现灯具" : "选择灯具";
        } else if (state.phase == Phase::Scanning)
            scan_result(p);
    } else if (e == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT && is_broadcast(brand)) {
        if (adv == Adv::StoppingIdle) {
            idle_stopped = true;
            adv = Adv::Idle;
        } else if (adv == Adv::Stopping) {
            if (!bursts.empty())
                bursts.pop_front();
            adv = Adv::Idle;
        }
    } else if (e == ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT && adv == Adv::Configuring) {
        if (p->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            adv = Adv::Idle;
            fail("广播配置失败");
            return;
        }
        esp_ble_adv_params_t a = {};
        a.adv_int_min = 0x20;
        a.adv_int_max = 0x30;
        a.adv_type = ADV_TYPE_IND;
        a.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
        a.channel_map = ADV_CHNL_ALL;
        a.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
        adv = Adv::Starting;
        if (esp_ble_gap_start_advertising(&a) != ESP_OK) {
            adv = Adv::Idle;
            fail("广播启动失败");
        }
    } else if (e == ESP_GAP_BLE_ADV_START_COMPLETE_EVT && adv == Adv::Starting) {
        if (p->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            adv = Adv::Idle;
            fail("广播启动失败");
            return;
        }
        adv = Adv::Sending;
        adv_until = now() + (bursts.empty() ? 250 : bursts.front().duration);
    }
}
void gatt_event(esp_gattc_cb_event_t e, esp_gatt_if_t i, esp_ble_gattc_cb_param_t *p) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (e == ESP_GATTC_REG_EVT) {
        if (p->reg.app_id == app_id && p->reg.status == ESP_GATT_OK)
            client = i;
        return;
    }
    if (i != client)
        return;
    if (e == ESP_GATTC_CONNECT_EVT) {
        if (!opening)
            return;
        opening = false;
        linked = true;
        conn = p->connect.conn_id;
        if (!active || !state.selected_valid ||
            memcmp(p->connect.remote_bda, state.selected.address.data(), 6)) {
            end_link();
            return;
        }
        service_start = service_end = 0;
        alternate_start = alternate_end = handshake_start = handshake_end = handshake_handle = 0;
        gatt_ready = false;
        esp_ble_gattc_search_service(client, conn, nullptr);
        return;
    }
    if (e == ESP_GATTC_OPEN_EVT && p->open.status != ESP_GATT_OK) {
        opening = false;
        if (active)
            fail("连接失败");
        return;
    }
    if (e == ESP_GATTC_CANCEL_OPEN_EVT) {
        opening = false;
        return;
    }
    if (e == ESP_GATTC_DISCONNECT_EVT) {
        if (!linked || p->disconnect.conn_id != conn) return;
        bool expected = closing;
        clear_link();
        if (active && !expected)
            fail("连接已断开");
        return;
    }
    if (!active || !linked || closing)
        return;
    switch (e) {
    case ESP_GATTC_SEARCH_RES_EVT: {
        auto pf = profile();
        if (same_uuid(p->search_res.srvc_id.uuid, pf.service)) {
            service_start = p->search_res.start_handle;
            service_end = p->search_res.end_handle;
        } else if (same_uuid(p->search_res.srvc_id.uuid, pf.alternate)) {
            alternate_start = p->search_res.start_handle;
            alternate_end = p->search_res.end_handle;
        } else if (brand == Brand::Ulanzi &&
                   same_uuid(p->search_res.srvc_id.uuid, ulanzi_handshake_uuid(0xc0))) {
            handshake_start = p->search_res.start_handle;
            handshake_end = p->search_res.end_handle;
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p->search_cmpl.status == ESP_GATT_OK && (service_start || alternate_start))
            discover();
        else {
            fail("不支持该灯具的 GATT 服务");
            end_link();
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (brand == Brand::Xsj && p->cfg_mtu.conn_id == conn && !gatt_ready) {
            if (p->cfg_mtu.status == ESP_GATT_OK && p->cfg_mtu.mtu >= 27)
                prepare_notifications();
            else {
                fail("灯具 MTU 协商失败");
                end_link();
            }
        }
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p->reg_for_notify.status != ESP_GATT_OK) {
            fail("状态订阅失败");
            end_link();
            break;
        }
        esp_gattc_descr_elem_t d{};
        uint16_t count = 1;
        if (esp_ble_gattc_get_descr_by_char_handle(client, conn, notify_handle,
                                                   uuid16(ESP_GATT_UUID_CHAR_CLIENT_CONFIG), &d,
                                                   &count) != ESP_GATT_OK ||
            !count) {
            if (profile().requires_notify) {
                fail("缺少通知描述符");
                end_link();
            } else
                start_query();
            break;
        }
        desc_handle = d.handle;
        uint8_t value[] = {cccd_mode, 0};
        if (esp_ble_gattc_write_char_descr(client, conn, desc_handle, 2, value, ESP_GATT_WRITE_TYPE_RSP,
                                           ESP_GATT_AUTH_REQ_NONE) != ESP_OK) {
            fail("状态订阅失败");
            end_link();
        }
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p->write.handle == desc_handle) {
            if (p->write.status == ESP_GATT_OK)
                start_query();
            else {
                fail("订阅失败");
                end_link();
            }
        }
        break;
    case ESP_GATTC_NOTIFY_EVT:
        if (p->notify.handle == notify_handle)
            on_notify(p->notify.value, p->notify.value_len);
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        if (handshake_handle && p->write.handle == handshake_handle) {
            handshake_handle = 0;
            if (p->write.status == ESP_GATT_OK)
                subscribe();
            else {
                fail("灯具握手失败");
                end_link();
            }
        } else if (writing && p->write.handle == write_handle) {
            writing = false;
            if (p->write.status != ESP_GATT_OK) {
                fail("灯具拒绝写入");
                end_link();
                break;
            }
            if (!writes.empty())
                writes.pop_front();
            next_send = now() + profile().interval;
        }
        break;
    default:
        break;
    }
}
} // namespace toolbox::lighting
