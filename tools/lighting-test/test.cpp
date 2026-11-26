#include "protocol.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
using namespace toolbox::lighting;
using Strings = std::vector<std::string>;
const std::map<std::string, Strings> vectors = {
#include "vectors.inc"
};
std::string hex(const Bytes &b) {
    std::string s;
    char c[3];
    for (auto x : b) {
        snprintf(c, 3, "%02x", x);
        s += c;
    }
    return s;
}
int passed = 0, failed = 0;
void verify(std::string name, const Frames &f) {
    Strings actual;
    for (auto &b : f)
        actual.push_back(hex(b));
    if (actual == vectors.at(name)) {
        ++passed;
        return;
    }
    ++failed;
    printf("FAIL %s\n actual  ", name.c_str());
    for (auto &h : actual)
        printf(" %s", h.c_str());
    printf("\n expected");
    for (auto &h : vectors.at(name))
        printf(" %s", h.c_str());
    puts("");
}
void verify(std::string name, const MeshFrames &f) {
    Frames b;
    for (auto &x : f)
        b.push_back(x.payload);
    verify(name, b);
}
int main() {
    State on;
    on.brightness = 374;
    on.kelvin = 4300;
    on.hue = 221;
    on.saturation = 67;
    on.tint = -4;
    on.speed = 60;
    State hsi = on;
    hsi.mode = Mode::Hsi;
    State off = on;
    off.power = false;
    InkeeAddress a{0x1234, 1, 2};
    verify("inkee_pair", Frames{inkee_frame(a, InkeeCommand::Pair, 0, 0x42)});
    verify("inkee_power", Frames{inkee_frame(a, InkeeCommand::Power, 1, 0x42)});
    verify("inkee_brightness", Frames{inkee_frame(a, InkeeCommand::Brightness, 37, 0x42)});
    verify("inkee_temperature", Frames{inkee_frame(a, InkeeCommand::Temperature, 4300, 0x42)});
    verify("lyd_cct", Frames{lyd_frame(13, 2, on)});
    verify("lyd_hsi", Frames{lyd_frame(13, 2, hsi)});
    verify("lyd_off", Frames{lyd_frame(13, 2, off)});
    struct Variant {
        YnVariant v;
        const char *name;
    };
    for (auto var : {Variant{YnVariant::Modern, "modern"}, Variant{YnVariant::Decimal, "modern-decimal"},
                     Variant{YnVariant::Legacy, "legacy"}, Variant{YnVariant::LegacyRgb, "legacy-rgb"},
                     Variant{YnVariant::Classic360, "legacy-360"}}) {
        auto c = yn_capability(var.v == YnVariant::Classic360 ? "YN360" : "YNLUX100RGB", 1044, var.v);
        for (auto s : {on, hsi, off})
            verify(std::string("yn_") + var.name + "_" +
                       (!s.power              ? "off"
                        : s.mode == Mode::Hsi ? "hsi"
                                              : "cct"),
                   yn_control(var.v, 7, c, s));
    }
    auto sid = find_product(Brand::Sidus, "400K5")->caps;
    auto god = find_product(Brand::Godox, "00DC")->caps;
    auto lum = find_product(Brand::Lumin, "32")->caps;
    auto jin = find_product(Brand::Jinbei, "17")->caps;
    jin.fx = true;
    auto ul = find_product(Brand::Ulanzi, "0D&07")->caps;
    ul.min_tint = -10;
    ul.max_tint = 10;
    for (auto s : {on, hsi, off}) {
        std::string name = !s.power ? "off" : s.mode == Mode::Hsi ? "hsi" : "cct";
        verify("sidus_" + name, sidus_control(sid, s));
        verify("godox_" + name, godox_control(god, s));
        verify("lumin_" + name, lumin_control(lum, s));
        verify("jinbei_" + name, jinbei_control(jin, s));
        verify("ulanzi_" + name, ulanzi_control(ul, s));
        uint16_t seq = 0;
        verify("zhiyun_" + name, zhiyun_control(0x8003, seq, s));
    }
    XsjSession xsj{{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}};
    verify("xsj_join", xsj_join(xsj));
    verify("xsj_control", xsj_control(xsj, on));
    NanliteProfile n{{2700, 7500, -10, 10, true, true, false, false}, 1, 1, 8};
    uint8_t roll = 0;
    verify("nanlite_cct", nanlite_usr_wrap(roll, nanlite_payload(n, 257, on)));
    verify("nanlite_hsi", nanlite_usr_wrap(roll, nanlite_payload(n, 257, hsi)));
    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
