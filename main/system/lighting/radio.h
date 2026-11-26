#pragma once
#include "protocol.h"
#include <array>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <string>

namespace toolbox::lighting {
struct Device {
    Brand brand = Brand::Inkee;
    std::array<uint8_t, 6> address{};
    uint8_t address_type = 0;
    int rssi = 0;
    char name[40]{};
    char product[12]{};
    Capability caps{};
    uint16_t channel = 0;
    uint8_t group = 0;
    uint16_t controller = 0;
    YnVariant variant = YnVariant::Modern;
    uint16_t target = 0xffff;
    uint16_t mesh_address = 0;
    uint16_t mesh_element = 0;
    uint16_t mesh_company = 0;
    uint16_t mesh_model = 0;
};
enum class Phase { Off, Starting, Scanning, Connecting, Ready, Sending, Error };
struct RadioState {
    Phase phase = Phase::Off;
    std::string message;
    std::vector<Device> devices;
    Device selected;
    bool selected_valid = false;
    bool channel_known = false;
};
const char *brand_name(Brand brand);
bool is_broadcast(Brand brand);
bool is_mesh(Brand brand);
void start(Brand brand);
void stop();
void tick();
RadioState status();
void connect(const Device &device);
void set_channel(uint16_t channel);
void send(const State &state, bool force = false);
void pair_inkee();
bool owns_radio();
esp_err_t stack_started();
void stack_stopping();
void gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *data);
void gatt_event(esp_gattc_cb_event_t event, esp_gatt_if_t interface, esp_ble_gattc_cb_param_t *data);
} // namespace toolbox::lighting
