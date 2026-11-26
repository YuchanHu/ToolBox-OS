#include "protocol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <numeric>
#include <string>

namespace toolbox::lighting {
namespace {
uint8_t lo(unsigned x) { return x & 255; }
uint8_t hi(unsigned x) { return (x >> 8) & 255; }
int limit(int x, int low, int high) { return std::clamp(x, low, high); }
int pct(const State &s) { return limit((s.brightness + 5) / 10, 0, 100); }
int kelvin(const Capability &c, const State &s) { return limit(s.kelvin, c.min_kelvin, c.max_kelvin); }
int tint(const Capability &c, const State &s) { return limit(s.tint, c.min_tint, c.max_tint); }
int saturation(const State &s) { return limit(s.saturation, 0, 100); }
int hue(const State &s) { return s.hue % 360; }
bool color(const Capability &c, const State &s) { return c.hsi && (s.mode == Mode::Hsi || !c.cct); }
unsigned sum(const Bytes &b, size_t start = 0, size_t end = SIZE_MAX) {
    unsigned n = 0;
    for (size_t i = start; i < std::min(end, b.size()); ++i)
        n += b[i];
    return n;
}
void be(Bytes &b, unsigned n) {
    b.push_back(hi(n));
    b.push_back(lo(n));
}
void le(Bytes &b, unsigned n) {
    b.push_back(lo(n));
    b.push_back(hi(n));
}
std::string compact(std::string_view v) {
    std::string result;
    for (unsigned char c : v)
        if (std::isalnum(c))
            result += std::toupper(c);
    return result;
}
bool begins(const std::string &a, std::string_view b) { return a.compare(0, b.size(), b) == 0; }
bool has(const std::string &a, std::string_view b) { return a.find(b) != std::string::npos; }
struct YnProduct {
    uint16_t code;
    const char *name;
    bool decimal;
    bool color = true;
    bool tint = true;
    bool effects = true;
    uint16_t min_kelvin = 2000;
    uint16_t max_kelvin = 10000;
};
constexpr YnProduct yn_products[] = {
    {287, "YNPAD100RGB", true}, {1044, "YNLUX100RGB", true}, {1051, "YNLUX300RGB", true},
    {1053, "YN150ULTRARGB", true}, {247, "YN360IV", false}, {244, "P360PROMAX", false},
    {235, "YN360MINI", false, true, false, false, 2500, 7500},
    {1052, "YN150ULTRABI", true, false, false, false, 2700, 6500}
};
const YnProduct *yn_product(const std::string &name, uint16_t code) {
    for (const auto &product : yn_products)
        if (product.code == code)
            return &product;
    for (const auto &product : yn_products)
        if (has(name, product.name))
            return &product;
    return nullptr;
}
bool changed(const State *p, const State &s) { return !p || !p->power || p->mode != s.mode; }
bool turn_on(const State *p) { return !p || !p->power; }
bool turn_off(const State *p) { return !p || p->power; }
Bytes sidus(uint8_t cmd) {
    Bytes d(10, 0);
    d[9] = cmd;
    return d;
}
MeshFrame sidus_finish(Bytes data) {
    data[0] = lo(sum(data));
    return {0x26, std::move(data)};
}
uint8_t crc8(const Bytes &b) {
    uint8_t crc = 0;
    for (uint8_t v : b) {
        crc ^= v;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1) ? 0x8c : 0);
    }
    return crc;
}
MeshFrame godox(Bytes data) {
    data.push_back(crc8(data));
    return {0xf01102, std::move(data)};
}
MeshFrame lumin(Bytes data) {
    le(data, sum(data));
    return {0xc086ae, std::move(data)};
}
MeshFrame jinbei(uint8_t cmd, Bytes data) {
    Bytes packet{0x7e, cmd, static_cast<uint8_t>(data.size() + 2)};
    packet.insert(packet.end(), data.begin(), data.end());
    packet.push_back(0x7e);
    return {0xca5d00, std::move(packet)};
}
Bytes ulanzi(uint8_t cmd, Bytes data) {
    Bytes packet{0xff, cmd, static_cast<uint8_t>(data.size())};
    packet.insert(packet.end(), data.begin(), data.end());
    packet.push_back(lo(sum(data)));
    packet.push_back(0xaa);
    return packet;
}
uint16_t crc16(const Bytes &data, size_t start) {
    uint16_t rem = 0;
    for (size_t i = start; i < data.size(); ++i) {
        rem ^= uint16_t(data[i]) << 8;
        for (int j = 0; j < 8; ++j)
            rem = (rem << 1) ^ ((rem & 0x8000) ? 0x1021 : 0);
    }
    return rem;
}
Bytes zhiyun(uint16_t seq, uint16_t cmd, const Bytes &body) {
    Bytes packet{0x24, 0x3c};
    le(packet, body.size() + 6);
    le(packet, 0x0100);
    le(packet, seq);
    le(packet, cmd);
    packet.insert(packet.end(), body.begin(), body.end());
    le(packet, crc16(packet, 4));
    return packet;
}
Bytes xsj(XsjSession &s, const Bytes &params) {
    Bytes packet{0x88,
                 0x99,
                 lo(params.size() + 0x0f),
                 0xff,
                 lo(params.size() + 0x10),
                 s.flag,
                 s.sequence,
                 3,
                 0x55,
                 0x57,
                 0x4c,
                 0x5a,
                 s.mac[1],
                 s.mac[0],
                 0x3a,
                 6};
    packet.insert(packet.end(), params.begin(), params.end());
    unsigned check = sum(packet, 7);
    unsigned high = 2 * check + sum(packet, 2, 7) + 0x83;
    packet.push_back(lo(check + 0x86));
    packet.push_back(lo(high));
    if (++s.sequence > 0x7f) {
        s.sequence = 0x60;
        s.flag ^= 0x40;
    }
    return packet;
}
} // namespace
Bytes inkee_frame(InkeeAddress a, InkeeCommand cmd, int value, uint8_t serial) {
    if (a.group < 2 || (a.controller == 0xffff && a.scene == 0xff))
        return {};
    Bytes b{0x7f, 0x6d, 0x3f, 0x65, serial, 6};
    if (cmd == InkeeCommand::Pair) {
        b.insert(b.end(),
                 {0xff, 0xff, 0xff, 0xfe, 0x10, lo(a.controller), hi(a.controller), a.scene, a.group, 0});
        return b;
    }
    b.insert(b.end(), {lo(a.controller), hi(a.controller), a.scene, a.group});
    switch (cmd) {
    case InkeeCommand::Power:
        b.insert(b.end(), {uint8_t(value ? 0x41 : 0x42), 0, 1, 1, 0, 0});
        break;
    case InkeeCommand::Brightness:
        b.insert(b.end(), {0x43, 0, 1, 0, uint8_t(limit(value, 0, 100)), 0});
        break;
    case InkeeCommand::Temperature:
        b.insert(b.end(), {0x46, 0, 1, uint8_t((limit(value, 2700, 6500) - 2700) / 38), 0, 0});
        break;
    default:
        break;
    }
    return b;
}
Bytes lyd_frame(uint8_t ch, uint8_t g, const State &s) {
    if (ch < 1 || ch > 100 || g > 5)
        return {};
    unsigned major = g, minor = 0;
    if (s.mode == Mode::Hsi) {
        major += 0x8000 + (saturation(s) << 8);
        minor = limit(s.hue, 0, 360);
    } else if (s.mode == Mode::Fx) {
        major += 8 * limit(s.effect, 1, 24);
    } else
        minor = 512 * limit((s.kelvin + 50) / 100, 25, 99);
    Bytes b{2, 0x15, 0xfa, 0xa5, 0, 0, 0x11, 0x11, 0xab, 0xcd, 1, 0x23, 0xbe, 0x89, 0, 0, 0, ch};
    be(b, major);
    be(b, minor);
    b.push_back(s.power ? pct(s) : 0);
    return b;
}
YnVariant yn_variant(std::string_view name, uint16_t product) {
    auto n = compact(name);
    if (begins(n, "YONGNUO"))
        n.erase(0, 7);
    if (n == "YN360" || n == "YN360RGB" || n == "YN360BICOLOR")
        return YnVariant::Classic360;
    for (auto prefix : {"YN208", "YN300", "YN320", "YN360", "YN600", "YN900", "YN860", "YN1200", "YN10800",
                        "YN128", "YN308", "YN508", "YN608", "YN808", "YN260"}) {
        if (begins(n, prefix) && !begins(n, "YN300IV") && !begins(n, "YN360IV") && !begins(n, "YN360MINI"))
            return has(n, "RGB") ? YnVariant::LegacyRgb : YnVariant::Legacy;
    }
    if (auto *known = yn_product(n, product); known && known->decimal)
        return YnVariant::Decimal;
    return YnVariant::Modern;
}
Capability yn_capability(std::string_view name, uint16_t id, YnVariant v) {
    Capability c;
    auto n = compact(name);
    const bool legacy = v == YnVariant::Legacy || v == YnVariant::LegacyRgb || v == YnVariant::Classic360;
    const auto *known = yn_product(n, id);
    const bool wide = (known && known->color) || has(n, "YN30SOFT");
    c.hsi = legacy ? (v != YnVariant::Legacy || has(n, "RGB"))
                   : (wide || has(n, "RGB") || v == YnVariant::ModernRgb);
    if (legacy) {
        c.min_kelvin = 3200;
        c.max_kelvin = 5600;
    } else if (known) {
        c.min_kelvin = known->min_kelvin;
        c.max_kelvin = known->max_kelvin;
    } else if (wide) {
        c.min_kelvin = 2000;
        c.max_kelvin = 10000;
    }
    if (id == 289 || has(n, "5600K") || has(n, "SINGLECOLORTEMP"))
        c.min_kelvin = c.max_kelvin = 5600;
    c.fx = !legacy && known && known->effects;
    if (!legacy && known && known->tint) {
        c.min_tint = -10;
        c.max_tint = 10;
    }
    c.decimal = v == YnVariant::Decimal;
    return c;
}
YnStatus yn_status(const uint8_t *d, size_t n) {
    if (!d || n < 2 || d[0] != 0xae)
        return {};
    if (d[1] == 0x23 && n >= 15 && d[6])
        return {d[6], uint16_t((d[3] << 8) | d[4]), false};
    if ((d[1] == 0xaa || d[1] == 0xee) && n == 6 && d[2])
        return {d[2], 0, true};
    return {};
}
Frames yn_query() { return {{0xae, 0x22, 4, 0x56}, {0xae, 0x33, 0, 0, 0, 0x56}}; }
Frames yn_control(YnVariant v, uint8_t ch, const Capability &c, const State &s, const State *p) {
    const bool classic = v == YnVariant::Classic360,
               old = classic || v == YnVariant::Legacy || v == YnVariant::LegacyRgb;
    ch = classic ? 1 : ch;
    if (!ch)
        return {};
    if (!s.power) {
        if (classic)
            return {{0xae, 0xee, 0, 0, 0, 0x56}};
        if (old)
            return {{0xae, 0xee, ch, 0, 0}};
        return {{0xae, 1, 6, ch, 0, 0x56}};
    }
    Frames out;
    if (!old && turn_on(p))
        out.push_back({0xae, 1, 6, ch, 1, 0x56});
    int i = limit(s.brightness, 0, 1000), b = c.decimal ? i / 10 : pct(s), fraction = i % 10;
    if (p && p->mode == Mode::Fx && p->effect && (s.mode != Mode::Fx || !s.effect))
        out.push_back({0xae, 9, 7, ch, p->effect, 1, 0x56});
    if (!old && c.fx && s.mode == Mode::Fx && s.effect) {
        out.push_back({0xae, 0x0d, 8, ch, s.effect, uint8_t(limit(s.speed, 0, 100)), uint8_t(pct(s))});
        if (turn_on(p) || !p || p->mode != Mode::Fx || p->effect != s.effect)
            out.push_back({0xae, 9, 7, ch, s.effect, 0, 0x56});
        return out;
    }
    if (color(c, s)) {
        int angle = hue(s), sat = saturation(s);
        if (old) {
            double brightness = i / 1000.0, chroma = brightness * sat / 100.0,
                   x = chroma * (1 - std::abs(std::fmod(angle / 60.0, 2.0) - 1)), m = brightness - chroma;
            const double channels[6][3] = {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x},
                                           {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}};
            Bytes rgb;
            for (double k : channels[angle / 60])
                rgb.push_back(uint8_t(std::lround((k + m) * (classic ? 255 : 99))));
            if (classic)
                out.push_back({0xae, 0xa1, rgb[0], rgb[1], rgb[2], 0x56});
            else
                out.push_back({0xae, 0x80, 0xa1, 0xaa, ch, rgb[0], rgb[1], rgb[2], 0, 0, 0, 0x56});
        } else {
            Bytes f{0xae, 7, 9, ch, hi(angle), lo(angle), uint8_t(sat), uint8_t(b)};
            if (c.decimal)
                f.insert(f.end(), {0xa0, uint8_t(fraction)});
            out.push_back(std::move(f));
        }
        return out;
    }
    int k = kelvin(c, s);
    if (old) {
        int cool = c.min_kelvin == c.max_kelvin ? b : std::lround(b * (k - 3200) / 2400.0);
        out.push_back({0xae, 0xaa, ch, uint8_t(cool), uint8_t(b - cool), 0x56});
    } else {
        Bytes f{0xae, 3, uint8_t(c.decimal ? 9 : 10), ch, hi(k), lo(k), uint8_t(b)};
        if (c.decimal)
            f.insert(f.end(), {0xa0, uint8_t(fraction)});
        else
            f.insert(f.end(), {0, 100});
        out.push_back(std::move(f));
        if (c.min_tint < c.max_tint)
            out.push_back({0xae, 0x0f, 7, ch, lo(tint(c, s)), 0x56});
    }
    return out;
}
MeshFrames sidus_control(const Capability &c, const State &s, const State *p) {
    MeshFrames out;
    if (!p || s.power != p->power) {
        auto d = sidus(0x8c);
        d[8] = s.power;
        out.push_back(sidus_finish(d));
    }
    if (!s.power)
        return out;
    auto settings = [&](const State &state) {
        int b = limit(state.brightness, 0, 1000), ib = (b & 3) << 6;
        Bytes d;
        if (color(c, state)) {
            int h = limit(state.hue, 0, 360), sa = saturation(state);
            d = sidus(0x81);
            d[5] = (sa & 3) << 6;
            d[6] = ((h & 7) << 5) | (sa >> 2);
            d[7] = ib | (h >> 3);
        } else if (!c.cct || c.max_kelvin == c.min_kelvin) {
            d = sidus(0x8f);
            d[7] = ib;
        } else {
            int k = (kelvin(c, state) + 5) / 10;
            bool ext = k > 1000;
            if (ext)
                k -= 1000;
            int gm = tint(c, state) + 10;
            d = sidus(0x82);
            d[5] = ((gm & 7) << 5) | (ext ? 4 : 0);
            d[6] = ((k & 15) << 4) | (gm >> 3);
            d[7] = ib | (k >> 4);
        }
        d[8] = b >> 2;
        return sidus_finish(d);
    };
    auto d = settings(s);
    if (turn_on(p) || settings(*p).payload != d.payload)
        out.push_back(std::move(d));
    return out;
}
MeshFrames godox_control(const Capability &c, const State &s, const State *p) {
    MeshFrames out;
    auto settings = [&](const State &v) {
        int b = limit(v.brightness, 0, 1000);
        if (!c.decimal)
            b = pct(v) * 10;
        int h = hue(v), t = tint(c, v);
        return color(c, v)
                   ? godox({0xf1, uint8_t(b / 10), lo(h), hi(h), uint8_t(saturation(v)), 0, uint8_t(b % 10)})
                   : godox({0xf0, uint8_t(b / 10), uint8_t((kelvin(c, v) + 50) / 100), lo(t + 50), 0, lo(t),
                            uint8_t(b % 10)});
    };
    if (s.power && (c.cct || c.hsi)) {
        auto d = settings(s);
        if (turn_on(p) || settings(*p).payload != d.payload)
            out.push_back(std::move(d));
    }
    if (!p || p->power != s.power) {
        auto f = godox({0xfe, uint8_t(s.power ? 0 : 1), 0xff, 0xff, 0xff, 0xff, 0xff});
        out.push_back(f);
        out.push_back(f);
    }
    return out;
}
MeshFrames lumin_control(const Capability &c, const State &s, const State *p) {
    if (!s.power)
        return turn_off(p) ? MeshFrames{lumin({0x0d, 0})} : MeshFrames{};
    MeshFrames out;
    if (turn_on(p))
        out.push_back(lumin({0x0d, 1}));
    bool ch = changed(p, s);
    int mode = color(c, s) ? 2 : (c.cct && c.min_kelvin != c.max_kelvin ? 1 : 0);
    if (mode == 1 && (ch || p->kelvin != s.kelvin || p->tint != s.tint)) {
        int k = kelvin(c, s);
        out.push_back(lumin({1, lo(k), hi(k), lo(tint(c, s) + 100)}));
    }
    if (mode == 2 && (ch || p->hue != s.hue || p->saturation != s.saturation)) {
        int h = limit(s.hue, 0, 360);
        out.push_back(lumin({2, lo(h), hi(h), uint8_t(saturation(s))}));
    }
    if (ch || p->brightness != s.brightness) {
        int b = limit(s.brightness, 0, 1000);
        out.push_back(lumin({0, lo(b), hi(b), uint8_t(mode)}));
    }
    return out;
}
MeshFrames jinbei_control(const Capability &c, const State &s, const State *p) {
    if (!s.power)
        return turn_off(p) ? MeshFrames{jinbei(3, {1})} : MeshFrames{};
    MeshFrames out;
    bool ch = changed(p, s);
    int mode = c.fx && s.mode == Mode::Fx && s.effect ? 2 : color(c, s) ? 1 : 0;
    if (turn_on(p))
        out.push_back(jinbei(1, {1}));
    if (mode == 2) {
        if (ch || p->brightness != s.brightness)
            out.push_back(jinbei(0x0f, {uint8_t(pct(s))}));
        if (ch)
            out.push_back(jinbei(0x3d, {2}));
        if (c.effect_speed && (ch || p->speed != s.speed))
            out.push_back(jinbei(9, {uint8_t(1 + std::lround(limit(s.speed, 0, 100) * 0.08)), 0}));
        if (c.cct && (ch || p->kelvin != s.kelvin))
            out.push_back(jinbei(0x0b, {uint8_t((kelvin(c, s) + 50) / 100)}));
        if (ch || p->effect != s.effect)
            out.push_back(jinbei(7, {s.effect}));
        if (ch)
            out.push_back(jinbei(0x2e, {1}));
        return out;
    }
    if ((p && p->mode == Mode::Fx) || (!p && c.fx))
        out.push_back(jinbei(0x2e, {0}));
    if (ch && (c.hsi || c.fx))
        out.push_back(jinbei(0x3d, {uint8_t(mode)}));
    if (mode == 1 && (ch || p->hue != s.hue || p->saturation != s.saturation)) {
        int h = hue(s);
        out.push_back(jinbei(0x0d, {hi(h), lo(h), uint8_t(saturation(s))}));
    }
    if (mode == 0 && c.cct && c.min_kelvin != c.max_kelvin) {
        if (ch || p->kelvin != s.kelvin)
            out.push_back(jinbei(0x0b, {uint8_t((kelvin(c, s) + 50) / 100)}));
        if (c.max_tint > c.min_tint && (ch || p->tint != s.tint))
            out.push_back(jinbei(0x13, {uint8_t(tint(c, s) + 10)}));
    }
    if (ch || p->brightness != s.brightness)
        out.push_back(jinbei(0x0f, {uint8_t(pct(s))}));
    return out;
}
Bytes ulanzi_query() { return ulanzi(0xa1, {0x30}); }
Frames ulanzi_control(const Capability &c, const State &s, const State *p) {
    if (!s.power)
        return turn_off(p) ? Frames{ulanzi(0xa1, {1})} : Frames{};
    Frames out;
    bool ch = changed(p, s);
    if (turn_on(p))
        out.push_back(ulanzi(0xa1, {0}));
    if (c.fx && s.mode == Mode::Fx && s.effect) {
        if (ch || p->effect != s.effect)
            out.push_back(ulanzi(0xa1, {s.effect}));
    } else if (color(c, s)) {
        if (ch)
            out.push_back(ulanzi(0xa1, {3}));
        if (ch || p->hue != s.hue || p->saturation != s.saturation) {
            int h = hue(s);
            out.push_back(ulanzi(0xa4, {hi(h), lo(h), uint8_t(saturation(s))}));
        }
    } else {
        if (ch)
            out.push_back(ulanzi(0xa1, {2}));
        if (c.cct && c.min_kelvin != c.max_kelvin && (ch || p->kelvin != s.kelvin)) {
            int k = kelvin(c, s);
            out.push_back(ulanzi(0xa3, {hi(k), lo(k)}));
        }
        if (c.min_tint < c.max_tint && (ch || p->tint != s.tint))
            out.push_back(ulanzi(0xa5, {uint8_t(tint(c, s) + 10)}));
    }
    if (ch || p->brightness != s.brightness)
        out.push_back(ulanzi(0xa2, {uint8_t(pct(s))}));
    return out;
}
Bytes zhiyun_query_id(uint16_t seq) { return zhiyun(seq, 0x2005, {}); }
Bytes zhiyun_query_info(uint16_t seq) { return zhiyun(seq, 0x2003, {}); }
Frames zhiyun_control(uint16_t target, uint16_t &seq, const State &s, const State *p) {
    if (target == 0xffff)
        return {};
    Frames out;
    int i = s.power ? pct(s) : 0;
    int old = p && p->power ? pct(*p) : 0;
    int k = limit((s.kelvin + 50) / 100, 27, 65) * 100;
    if (i && (!old || !p || p->kelvin != s.kelvin)) {
        Bytes b{lo(target), hi(target), 1, lo(k), hi(k)};
        out.push_back(zhiyun(++seq, 0x1002, b));
    }
    if (!p || i != old) {
        float f = i;
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        Bytes b{lo(target), hi(target), 1, lo(bits), hi(bits), lo(bits >> 16), hi(bits >> 16)};
        out.push_back(zhiyun(++seq, 0x1001, b));
    }
    return out;
}
Frames xsj_join(XsjSession &s) {
    Bytes login{0x88, 0x99, 0x12, 0xff, 0x13, 1, 0x61, 5, 0x55, 0x57, 0x4c, 0x5a};
    login.insert(login.end(), s.mac.rbegin(), s.mac.rend());
    login.insert(login.end(), {s.mac[1], s.mac[0]});
    login.push_back(lo(sum(login, 8) + 0x86));
    return {login, xsj(s, {0x26, 1, 0})};
}
Frames xsj_control(XsjSession &s, const State &state) {
    Frames out{xsj(s, {0, 1, uint8_t(state.power)})};
    if (state.power) {
        int k = limit(state.kelvin, 2700, 6500);
        for (int i = 0; i < 3; ++i)
            out.push_back(xsj(s, {0x14, 4, uint8_t(pct(state)), hi(k), lo(k), 0x0a}));
    }
    return out;
}
Bytes nanlite_payload(const NanliteProfile &p, uint16_t ch, const State &s) {
    if (ch > 0x7fff)
        return {};
    int b = s.power ? pct(s) : 0, k = kelvin(p.caps, s), t = tint(p.caps, s) * 10;
    Bytes d{0x24, hi(ch), lo(ch), 1, 1, 1};
    if (p.hsi_length && color(p.caps, s)) {
        int h = limit(s.hue, 0, 360);
        d.insert(d.end(), {p.hsi_length, hi(b), lo(b), 3, 1, hi(h), lo(h), uint8_t(saturation(s))});
        if (p.hsi_length == 9 || p.hsi_length == 0x10)
            be(d, k);
    } else if (p.caps.cct)
        d.insert(d.end(), {0x0b, hi(b), lo(b), 2, 2, p.output, hi(k), lo(k), hi(t), lo(t), p.source});
    else if (p.caps.min_tint < p.caps.max_tint)
        d.insert(d.end(), {7, hi(b), lo(b), 1, 1, hi(t), lo(t)});
    else
        d.insert(d.end(), {5, hi(b), lo(b), 1, 1});
    return d;
}
Frames nanlite_usr_wrap(uint8_t &roll, const Bytes &data) {
    Bytes b{0xfa, 0x55, hi(data.size() + 10), lo(data.size() + 10), 1, uint8_t(roll & 7)};
    roll = (roll + 1) & 7;
    b.insert(b.end(), data.begin(), data.end());
    be(b, sum(b) + 1);
    b.insert(b.end(), {0x0d, 0x0a});
    Frames frames;
    for (size_t i = 0; i < b.size(); i += 20)
        frames.emplace_back(b.begin() + i, b.begin() + std::min(i + 20, b.size()));
    return frames;
}
Frames nanlite_usr_join(uint8_t &roll, uint32_t elapsed) {
    return nanlite_usr_wrap(roll,
                            {0x20, 1, 0x64, hi(elapsed >> 16), lo(elapsed >> 16), hi(elapsed), lo(elapsed)});
}
} // namespace toolbox::lighting
