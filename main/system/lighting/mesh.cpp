#include "mesh.h"
#include <esp_gap_ble_api.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <esp_ble_mesh_ble_api.h>
#include <esp_ble_mesh_common_api.h>
#include <esp_ble_mesh_config_model_api.h>
#include <esp_ble_mesh_defs.h>
#include <esp_ble_mesh_networking_api.h>
#include <esp_ble_mesh_provisioning_api.h>
#include <esp_ble_mesh_proxy_api.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <map>
#include <mutex>
#include <nvs.h>

namespace toolbox::lighting::mesh {
namespace {
constexpr uint16_t network = 0, app_key_index = 0, local_address = 1;
constexpr uint16_t local_model_id = 0x1001, sig_model_id = 0x7ffe, cid = 0x02e5;
constexpr auto bearers = esp_ble_mesh_prov_bearer_t(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
std::recursive_mutex mutex;
RadioState radio;
Brand brand;
bool active = false, initialized = false, ready = false, configuring = false, key_bound = false;
bool proxy_opening = false, proxy_link = false;
uint8_t proxy_handle = 0;
uint32_t deadline = 0, next_send = 0, init_at = 0, config_retry_at = 0;
uint8_t app_key[16]{}, uuid[16]{};
State previous, requested;
bool previous_valid = false, pending = false;
std::deque<MeshFrame> frames;
struct Candidate {
    Device device;
    esp_ble_mesh_unprov_dev_add_t add{};
    bool has_beacon = false;
};
std::map<std::array<uint8_t, 6>, Candidate> candidates;
esp_ble_mesh_client_t cfg_client{}, send_client{}, sidus_client{};
unsigned models_bound = 0;
esp_ble_mesh_cfg_srv_t cfg_server{};
esp_ble_mesh_model_op_t sidus_ops[] = {ESP_BLE_MESH_MODEL_OP(0x26, 10), ESP_BLE_MESH_MODEL_OP_END};
esp_ble_mesh_model_op_t send_ops[] = {ESP_BLE_MESH_MODEL_OP(0xf00211, 1), ESP_BLE_MESH_MODEL_OP(0xc0ae86, 1),
                                      ESP_BLE_MESH_MODEL_OP(0xca005d, 1), ESP_BLE_MESH_MODEL_OP(0xcd005d, 1),
                                      ESP_BLE_MESH_MODEL_OP_END};
esp_ble_mesh_model_t sig_models[] = {ESP_BLE_MESH_MODEL_CFG_SRV(&cfg_server),
                                     ESP_BLE_MESH_MODEL_CFG_CLI(&cfg_client),
                                     ESP_BLE_MESH_SIG_MODEL(sig_model_id, sidus_ops, nullptr, &sidus_client)};
esp_ble_mesh_model_t client_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(cid, local_model_id, send_ops, nullptr, &send_client)};
esp_ble_mesh_elem_t elements[] = {ESP_BLE_MESH_ELEMENT(0, sig_models, client_models)};
esp_ble_mesh_comp_t comp{};
esp_ble_mesh_prov_t prov{.prov_uuid = uuid, .prov_unicast_addr = local_address, .prov_start_address = 2};
uint32_t now() { return esp_timer_get_time() / 1000; }
bool due(uint32_t d) { return int32_t(now() - d) >= 0; }
void msg(Phase phase, const char *text) {
    radio.phase = phase;
    radio.message = text;
}
void error(const char *text) {
    ESP_LOGW("LightMesh", "%s", text);
    msg(Phase::Error, text);
    deadline = 0;
    configuring = false;
    frames.clear();
    pending = false;
    previous_valid = false;
}
uint16_t word(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
void set_product(Device &d, const Product *p) {
    if (!p)
        return;
    snprintf(d.name, sizeof(d.name), "%s", p->name);
    snprintf(d.product, sizeof(d.product), "%s", p->id);
    d.caps = p->caps;
}
void publish(Candidate &c) {
    if (!c.has_beacon || !c.device.name[0] || radio.phase != Phase::Scanning)
        return;
    auto it = std::find_if(radio.devices.begin(), radio.devices.end(),
                           [&](const Device &o) { return o.address == c.device.address; });
    if (it != radio.devices.end())
        *it = c.device;
    else if (radio.devices.size() < 16)
        radio.devices.push_back(c.device);
}
void update_adv(const esp_ble_mesh_ble_adv_rpt_t &a) {
    std::array<uint8_t, 6> addr{};
    std::copy(a.addr, a.addr + 6, addr.begin());
    auto &c = candidates[addr];
    auto &d = c.device;
    d.address = addr;
    d.address_type = a.addr_type;
    d.rssi = a.rssi;
    d.brand = brand;
    char name[48]{};
    char model_id[12]{};
    for (size_t i = 0; i < a.length;) {
        size_t n = a.data[i];
        if (!n || i + n >= a.length)
            break;
        const uint8_t *data = a.data + i + 2;
        uint8_t type = a.data[i + 1];
        if (type == 8 || type == 9)
            memcpy(name, data, std::min(n - 1, sizeof(name) - 1));
        else if (type == 0xff && n > 10 && brand == Brand::Godox)
            snprintf(model_id, sizeof(model_id), "%02X%02X", data[7], data[6]);
        i += n + 1;
    }
    if (brand == Brand::Godox && strcmp(name, "GD_LED") == 0) {
        if (auto *p = find_product(brand, model_id))
            set_product(d, p);
    } else if (brand == Brand::Sidus && name[0]) {
        char product[6]{};
        memcpy(product, name, 5);
        if (auto *p = find_product(brand, product))
            set_product(d, p);
        else if (auto *p = find_product(brand, name))
            set_product(d, p);
    } else if ((brand == Brand::Lumin || brand == Brand::Jinbei) && name[0]) {
        if (auto *p = find_product(brand, name))
            set_product(d, p);
        else {
            char *sep = strrchr(name, '-');
            if (sep && strlen(sep + 1) >= 4) {
                *sep = 0;
                if (auto *p = find_product(brand, name))
                    set_product(d, p);
            }
        }
    }
    publish(c);
    if (candidates.size() > 64) {
        for (auto it = candidates.begin(); it != candidates.end(); ++it)
            if (!it->second.has_beacon) {
                candidates.erase(it);
                break;
            }
    }
}
void ble_cb(esp_ble_mesh_ble_cb_event_t e, esp_ble_mesh_ble_cb_param_t *p) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active)
        return;
    if (e == ESP_BLE_MESH_SCAN_BLE_ADVERTISING_PKT_EVT && radio.phase == Phase::Scanning)
        update_adv(p->scan_ble_adv_pkt);
}
esp_ble_mesh_client_common_param_t config_params(uint32_t op) {
    esp_ble_mesh_client_common_param_t c{};
    c.opcode = op;
    c.model = cfg_client.model;
    c.ctx.net_idx = network;
    c.ctx.app_idx = app_key_index;
    c.ctx.addr = radio.selected.mesh_address;
    c.ctx.send_ttl = 5;
    c.msg_timeout = 10000;
    return c;
}
void composition_request() {
    auto c = config_params(ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    esp_ble_mesh_cfg_client_get_state_t g{};
    g.comp_data_get.page = 0;
    msg(Phase::Connecting, "读取 Mesh 模型");
    if (esp_ble_mesh_config_client_get_state(&c, &g) != ESP_OK)
        error("读取 Mesh 模型失败");
}
bool model_allowed(uint16_t company, uint16_t model) {
    switch (brand) {
    case Brand::Sidus:
        return (company == 0x0211 && model == 0) || (company == 0x03f6 && model == 0x1000);
    case Brand::Godox:
        return company == 0x0211 && model == 0;
    case Brand::Lumin:
        return company == 0xae86 && model == 1;
    case Brand::Jinbei:
        return company == 0x005d;
    default:
        return false;
    }
}
bool parse_composition(const uint8_t *bytes, size_t len) {
    if (len < 10)
        return false;
    size_t i = 10;
    uint16_t element = radio.selected.mesh_address;
    while (i + 4 <= len) {
        uint8_t sig = bytes[i + 2], vnd = bytes[i + 3];
        i += 4;
        if (i + 2 * sig + 4 * vnd > len)
            return false;
        i += 2 * sig;
        for (int v = 0; v < vnd; v++, i += 4) {
            uint16_t company = word(bytes + i), model = word(bytes + i + 2);
            if (model_allowed(company, model)) {
                radio.selected.mesh_company = company;
                radio.selected.mesh_model = model;
                radio.selected.mesh_element = element;
                return true;
            }
        }
        ++element;
    }
    return false;
}
void bind_model() {
    auto c = config_params(ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
    esp_ble_mesh_cfg_client_set_state_t s{};
    s.model_app_bind.element_addr = radio.selected.mesh_element;
    s.model_app_bind.model_app_idx = app_key_index;
    s.model_app_bind.company_id = radio.selected.mesh_company;
    s.model_app_bind.model_id = radio.selected.mesh_model;
    msg(Phase::Connecting, "绑定控制模型");
    if (esp_ble_mesh_config_client_set_state(&c, &s) != ESP_OK)
        error("绑定控制模型失败");
}
void cfg_cb(esp_ble_mesh_cfg_client_cb_event_t e, esp_ble_mesh_cfg_client_cb_param_t *p) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active || !configuring || !p->params || p->params->ctx.addr != radio.selected.mesh_address)
        return;
    if (p->error_code || e == ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT) {
        error("Mesh 配置响应超时");
        return;
    }
    switch (p->params->opcode) {
    case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET: {
        if (e != ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT)
            return;
        auto *data = p->status_cb.comp_data_status.composition_data;
        if (!data || !parse_composition(data->data, data->len)) {
            error("未找到支持的灯光控制模型");
            return;
        }
        auto c = config_params(ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
        esp_ble_mesh_cfg_client_set_state_t s{};
        s.app_key_add.net_idx = network;
        s.app_key_add.app_idx = app_key_index;
        memcpy(s.app_key_add.app_key, app_key, 16);
        msg(Phase::Connecting, "写入 Mesh 应用密钥");
        if (esp_ble_mesh_config_client_set_state(&c, &s) != ESP_OK)
            error("应用密钥写入失败");
        break;
    }
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        if (e == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT) {
            if (p->status_cb.appkey_status.status) {
                error("灯具拒绝应用密钥");
                return;
            }
            bind_model();
        }
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        if (e == ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT) {
            if (p->status_cb.model_app_status.status) {
                error("灯具拒绝模型绑定");
                return;
            }
            configuring = false;
            deadline = 0;
            radio.selected_valid = true;
            msg(Phase::Ready, "Mesh 已绑定");
        }
        break;
    default:
        break;
    }
}
void maybe_ready() {
    if (ready && key_bound && radio.phase == Phase::Starting) {
        msg(Phase::Scanning, "请选择配网灯具");
        esp_ble_mesh_ble_scan_param_t p{};
        p.duration = 0;
        esp_ble_mesh_start_ble_scanning(&p);
    }
}
void prov_cb(esp_ble_mesh_prov_cb_event_t e, esp_ble_mesh_prov_cb_param_t *p) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active)
        return;
    switch (e) {
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        if (p->provisioner_prov_enable_comp.err_code)
            error("Mesh 启动失败");
        else {
            ready = true;
            maybe_ready();
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        if (p->provisioner_add_app_key_comp.err_code)
            error("Mesh 密钥初始化失败");
        else if (esp_ble_mesh_provisioner_bind_app_key_to_local_model(local_address, app_key_index,
                                                                      local_model_id, cid) != ESP_OK)
            error("本地模型绑定失败");
        break;
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        if (p->provisioner_bind_app_key_to_model_comp.err_code)
            error("本地模型绑定失败");
        else {
            if (++models_bound == 1) {
                if (esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                        local_address, app_key_index, sig_model_id, ESP_BLE_MESH_CID_NVAL) != ESP_OK)
                    error("本地模型绑定失败");
            } else {
                key_bound = true;
                maybe_ready();
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT: {
        if (radio.phase != Phase::Scanning)
            return;
        auto &r = p->provisioner_recv_unprov_adv_pkt;
        std::array<uint8_t, 6> addr{};
        std::copy(r.addr, r.addr + 6, addr.begin());
        auto &c = candidates[addr];
        auto &d = c.device;
        d.brand = brand;
        d.address = addr;
        d.address_type = r.addr_type;
        c.has_beacon = true;
        memcpy(c.add.addr, r.addr, 6);
        memcpy(c.add.uuid, r.dev_uuid, 16);
        c.add.addr_type = r.addr_type;
        c.add.oob_info = r.oob_info;
        c.add.bearer = r.bearer;
        if (brand == Brand::Sidus) {
            char id[6]{};
            memcpy(id, r.dev_uuid, 5);
            set_product(d, find_product(brand, id));
        }
        publish(c);
        break;
    }
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        if (p->provisioner_add_unprov_dev_comp.err_code)
            error("无法开始灯具入网");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        if (!configuring)
            return;
        radio.selected.mesh_address = p->provisioner_prov_complete.unicast_addr;
        config_retry_at = now() + 1200;
        msg(Phase::Connecting, "灯具入网成功");
        break;
    case ESP_BLE_MESH_PROXY_CLIENT_RECV_ADV_PKT_EVT:
        if (configuring && radio.selected.mesh_address && !proxy_opening && !proxy_link) {
            auto &r = p->proxy_client_recv_adv_pkt;
            if (r.net_idx == network) {
                proxy_opening = esp_ble_mesh_proxy_client_connect(r.addr, r.addr_type, network) == ESP_OK;
            }
        }
        break;
    case ESP_BLE_MESH_PROXY_CLIENT_CONNECTED_EVT: {
        proxy_opening = false;
        proxy_link = true;
        proxy_handle = p->proxy_client_connected.conn_handle;
        uint16_t filter = local_address;
        esp_ble_mesh_proxy_client_add_filter_addr(proxy_handle, network, &filter, 1);
        break;
    }
    case ESP_BLE_MESH_PROXY_CLIENT_CONNECT_COMP_EVT:
        if (p->proxy_client_connect_comp.err_code)
            proxy_opening = false;
        break;
    case ESP_BLE_MESH_PROXY_CLIENT_DISCONNECTED_EVT:
        proxy_link = proxy_opening = false;
        break;
    default:
        break;
    }
}
void model_cb(esp_ble_mesh_model_cb_event_t e, esp_ble_mesh_model_cb_param_t *p) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (active && e == ESP_BLE_MESH_MODEL_SEND_COMP_EVT && p->model_send_comp.err_code)
        error("Mesh 报文发送失败");
}
void init() {
    cfg_server.net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20);
    cfg_server.relay = ESP_BLE_MESH_RELAY_DISABLED;
    cfg_server.beacon = ESP_BLE_MESH_BEACON_DISABLED;
    cfg_server.gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED;
    cfg_server.friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED;
    cfg_server.default_ttl = 5;
    comp.cid = cid;
    comp.element_count = 1;
    comp.elements = elements;
    esp_fill_random(uuid, 16);
    prov.prov_uuid = uuid;
    prov.prov_start_address = 2;
    prov.prov_algorithm = 0;
    prov.iv_index = 0;
    esp_ble_mesh_register_prov_callback(prov_cb);
    esp_ble_mesh_register_config_client_callback(cfg_cb);
    esp_ble_mesh_register_custom_model_callback(model_cb);
    esp_ble_mesh_register_ble_callback(ble_cb);
    if (esp_ble_mesh_init(&prov, &comp) != ESP_OK) {
        error("Mesh 初始化失败");
        return;
    }
    initialized = true;
    if (esp_ble_mesh_client_model_init(&client_models[0]) != ESP_OK ||
        esp_ble_mesh_client_model_init(&sig_models[2]) != ESP_OK) {
        error("Mesh 客户端初始化失败");
        return;
    }
    if (esp_ble_mesh_provisioner_prov_enable(bearers) != ESP_OK) {
        error("Mesh 配网服务启动失败");
        return;
    }
    if (auto key = esp_ble_mesh_provisioner_get_local_app_key(network, app_key_index)) {
        memcpy(app_key, key, 16);
        if (esp_ble_mesh_provisioner_bind_app_key_to_local_model(local_address, app_key_index, local_model_id,
                                                                 cid) != ESP_OK)
            error("本地模型绑定失败");
    } else {
        esp_fill_random(app_key, 16);
        if (esp_ble_mesh_provisioner_add_local_app_key(app_key, network, app_key_index) != ESP_OK)
            error("Mesh 本地密钥生成失败");
    }
}
void pack(const State &s) {
    const State *p = previous_valid ? &previous : nullptr;
    MeshFrames f;
    auto &c = radio.selected.caps;
    switch (brand) {
    case Brand::Sidus:
        f = sidus_control(c, s, p);
        break;
    case Brand::Godox:
        f = godox_control(c, s, p);
        break;
    case Brand::Lumin:
        f = lumin_control(c, s, p);
        break;
    case Brand::Jinbei:
        f = jinbei_control(c, s, p);
        break;
    default:
        return;
    }
    for (auto &x : f)
        frames.push_back(std::move(x));
    previous = s;
    previous_valid = true;
    if (!frames.empty()) {
        msg(Phase::Sending, "正在发送 Mesh 指令");
        next_send = now();
    }
}
} // namespace
void start(Brand b) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    brand = b;
    active = true;
    radio = {};
    candidates.clear();
    ready = key_bound = configuring = previous_valid = pending = proxy_link = proxy_opening = false;
    models_bound = 0;
    config_retry_at = deadline = 0;
    frames.clear();
    msg(Phase::Starting, "启动 Mesh");
    esp_ble_gap_stop_advertising();
    init_at = now() + 160;
}
void stop() {
    bool deinit = false;
    {
        std::lock_guard<std::recursive_mutex> l(mutex);
        active = false;
        frames.clear();
        candidates.clear();
        pending = false;
        deinit = initialized;
        initialized = false;
        radio = {};
    }
    if (deinit) {
        esp_ble_mesh_stop_ble_scanning();
        if (proxy_link)
            esp_ble_mesh_proxy_client_disconnect(proxy_handle);
        esp_ble_mesh_client_model_deinit(&client_models[0]);
        esp_ble_mesh_client_model_deinit(&sig_models[2]);
        esp_ble_mesh_deinit_param_t p{};
        p.erase_flash = false;
        esp_ble_mesh_deinit(&p);
    }
}
RadioState status() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    return radio;
}
void connect(const Device &d) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active || !initialized || !key_bound || d.brand != brand || radio.phase != Phase::Scanning)
        return;
    radio.selected = d;
    radio.selected_valid = false;
    previous_valid = false;
    if (d.mesh_address && d.mesh_element && esp_ble_mesh_provisioner_get_node_with_addr(d.mesh_address)) {
        radio.selected_valid = true;
        msg(Phase::Ready, "Mesh 已绑定");
        return;
    }
    auto it = candidates.find(d.address);
    if (it == candidates.end() || !it->second.has_beacon) {
        error("请将灯具设置为配网状态");
        return;
    }
    auto add = it->second.add;
    configuring = true;
    deadline = now() + 90000;
    msg(Phase::Connecting, "正在入网");
    auto flags = esp_ble_mesh_dev_add_flag_t(ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG |
                                             ADD_DEV_FLUSHABLE_DEV_FLAG);
    if (esp_ble_mesh_provisioner_add_unprov_dev(&add, flags) != ESP_OK)
        error("Mesh 入网启动失败");
}
void send(const State &s, bool force) {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active || !radio.selected_valid || (radio.phase != Phase::Ready && radio.phase != Phase::Sending))
        return;
    if (force) previous_valid = false;
    requested = s;
    pending = true;
}
void tick() {
    std::lock_guard<std::recursive_mutex> l(mutex);
    if (!active)
        return;
    if (!initialized && radio.phase == Phase::Starting && due(init_at)) {
        init();
        return;
    }
    if (deadline && due(deadline)) {
        error("Mesh 入网或绑定超时");
        return;
    }
    if (config_retry_at && due(config_retry_at) && configuring) {
        config_retry_at = 0;
        composition_request();
    }
    if (radio.phase == Phase::Ready && pending) {
        pending = false;
        pack(requested);
    }
    if (radio.phase == Phase::Sending && due(next_send)) {
        if (frames.empty()) {
            msg(Phase::Ready, "指令已发送");
            return;
        }
        auto f = std::move(frames.front());
        frames.pop_front();
        uint32_t opcode = f.opcode < 0x100
                              ? f.opcode
                              : (f.opcode & 0xff0000) | ((f.opcode & 255) << 8) | ((f.opcode >> 8) & 255);
        esp_ble_mesh_msg_ctx_t ctx{};
        ctx.net_idx = network;
        ctx.app_idx = app_key_index;
        ctx.addr = radio.selected.mesh_element;
        ctx.send_ttl = 5;
        auto r = esp_ble_mesh_client_model_send_msg(opcode == 0x26 ? &sig_models[2] : &client_models[0], &ctx,
                                                    opcode, f.payload.size(), f.payload.data(), 0, false,
                                                    ROLE_PROVISIONER);
        if (r != ESP_OK)
            error("Mesh 发送失败");
        next_send = now() + 100;
    }
}
} // namespace toolbox::lighting::mesh
