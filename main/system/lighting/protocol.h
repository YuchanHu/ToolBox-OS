#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace toolbox::lighting {
// Transport independent command encoding. No radio, LVGL, or Android code here.
enum class Brand : uint8_t { Inkee, Yongnuo, Sidus, Lyd, Godox, Lumin, Jinbei, Xsj, Nanlite, Ulanzi, Zhiyun };
enum class Mode : uint8_t { Cct, Hsi, Fx };
enum class YnVariant : uint8_t { Modern, Decimal, ModernRgb, Legacy, LegacyRgb, Classic360 };
struct Capability {
    uint16_t min_kelvin = 2700;
    uint16_t max_kelvin = 6500;
    int16_t min_tint = 0;
    int16_t max_tint = 0;
    bool cct = true;
    bool hsi = false;
    bool fx = false;
    bool decimal = false;
    bool effect_speed = false;
};
struct State {
    bool power = true;
    uint16_t brightness = 500; // tenths of one percent, 0–1000
    uint16_t kelvin = 5600;
    uint16_t hue = 0;
    uint8_t saturation = 100;
    int16_t tint = 0;
    Mode mode = Mode::Cct;
    uint8_t effect = 0; // model-specific wire effect ID, zero = none
    uint8_t speed = 50;
};
using Bytes = std::vector<uint8_t>;
using Frames = std::vector<Bytes>;
struct MeshFrame {
    uint32_t opcode;
    Bytes payload;
};
using MeshFrames = std::vector<MeshFrame>;
struct InkeeAddress {
    uint16_t controller;
    uint8_t scene;
    uint8_t group;
};
enum class InkeeCommand { Pair, Power, Brightness, Temperature };
Bytes inkee_frame(InkeeAddress address, InkeeCommand command, int value, uint8_t serial);
Bytes lyd_frame(uint8_t channel, uint8_t group, const State &state);
YnVariant yn_variant(std::string_view name, uint16_t product = 0);
Capability yn_capability(std::string_view name, uint16_t product, YnVariant variant);
struct YnStatus {
    uint8_t channel = 0;
    uint16_t product = 0;
    bool legacy = false;
};
YnStatus yn_status(const uint8_t *bytes, size_t size);
Frames yn_query();
Frames yn_control(YnVariant variant, uint8_t channel, const Capability &caps, const State &state,
                  const State *previous = nullptr);
MeshFrames sidus_control(const Capability &caps, const State &state, const State *previous = nullptr);
MeshFrames godox_control(const Capability &caps, const State &state, const State *previous = nullptr);
MeshFrames lumin_control(const Capability &caps, const State &state, const State *previous = nullptr);
MeshFrames jinbei_control(const Capability &caps, const State &state, const State *previous = nullptr);
Frames ulanzi_control(const Capability &caps, const State &state, const State *previous = nullptr);
Bytes ulanzi_query();
Bytes zhiyun_query_id(uint16_t sequence);
Bytes zhiyun_query_info(uint16_t sequence);
Frames zhiyun_control(uint16_t target, uint16_t &sequence, const State &state,
                      const State *previous = nullptr);
struct XsjSession {
    std::array<uint8_t, 6> mac{};
    uint8_t sequence = 0x62;
    uint8_t flag = 0x0a;
};
Frames xsj_join(XsjSession &session);
Frames xsj_control(XsjSession &session, const State &state);
struct NanliteProfile {
    Capability caps;
    uint8_t output = 1;
    uint8_t source = 1;
    uint8_t hsi_length = 0;
};
const NanliteProfile *nanlite_profile(std::string_view id);
Bytes nanlite_payload(const NanliteProfile &profile, uint16_t channel, const State &state);
Frames nanlite_usr_wrap(uint8_t &roll, const Bytes &command);
Frames nanlite_usr_join(uint8_t &roll, uint32_t elapsed_ms);
struct Product {
    Brand brand;
    const char *id;
    const char *name;
    Capability caps;
};
const Product *find_product(Brand brand, std::string_view id_or_name);
const Product *products(size_t &count);
struct Effect { uint8_t code; const char* name; };
std::vector<Effect> effects_for(Brand brand, std::string_view product_id, const Capability& caps);
} // namespace toolbox::lighting
