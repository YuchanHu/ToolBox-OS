#include "sony/automation.h"
#include "sony/body.h"
#include "sony/capture.h"
#include "sony/media.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace sony;
using Buffer = std::vector<uint8_t>;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static void put(Buffer& b, uint64_t v, unsigned n) {
    while (n--) { b.push_back(uint8_t(v)); v >>= 8; }
}
static void append(Buffer& b, const Buffer& src) { b.insert(b.end(), src.begin(), src.end()); }
static void packet(Buffer& b, unsigned type, const Buffer& payload) {
    put(b, payload.size() + 8, 4); put(b, type, 4); append(b, payload);
}
static void string(Buffer& b, const char* s) {
    size_t n = strlen(s) + 1;
    put(b, n, 1);
    for (size_t i = 0; i < n; ++i) put(b, s[i], 2);
}
struct Peer : Wire {
    Buffer input[2], sent[2];
    size_t pos[2]{};
    bool opened[2]{};
    bool* cancelOnHalfPress = nullptr;
    bool open(Stream s) override { return !opened[int(s)] && (opened[int(s)] = true); }
    void deadline() override {}
    void pause(unsigned ms) override { if (ms == 500 && cancelOnHalfPress) *cancelOnHalfPress = false; }
    bool read(Stream s, void* out, size_t n) override {
        auto i = int(s);
        if (n > input[i].size() - pos[i]) return false;
        memcpy(out, input[i].data() + pos[i], n);
        pos[i] += n;
        return true;
    }
    bool write(Stream s, const void* data, size_t n) override {
        auto p = static_cast<const uint8_t*>(data);
        sent[int(s)].insert(sent[int(s)].end(), p, p + n);
        return true;
    }
    int tryEventByte(uint8_t& v) override { return read(Stream::Event, &v, 1) ? 1 : 0; }
    void reply(unsigned tid, Buffer data = {}, unsigned code = 0x2001) {
        if (!data.empty()) {
            Buffer start, chunk, end;
            put(start, tid, 4); put(start, data.size(), 8); packet(input[0], 9, start);
            put(chunk, tid, 4); chunk.push_back(data.front()); packet(input[0], 10, chunk);
            put(end, tid, 4); end.insert(end.end(), data.begin() + 1, data.end()); packet(input[0], 12, end);
        }
        Buffer response;
        put(response, code, 2); put(response, tid, 4); packet(input[0], 7, response);
    }
    Buffer props(unsigned raw) {
        Buffer data;
        put(data, 1, 8); append(data, {0x1d,0xd2,2,0,0,2,0,uint8_t(raw),2,0,0,3,0,0,1,2});
        return data;
    }
    void prepare(unsigned recording = 0, bool body = false, const Buffer* dataset = nullptr, bool media = false) {
        Buffer init(26);
        init[0] = 37;
        init[24] = 1;
        packet(input[0], 2, init); packet(input[1], 4, {});
        Buffer info(8);
        string(info, "Sony"); put(info, 0, 2);
        put(info, media ? 5 : 1, 4); put(info, 0x9201, 2);
        if (media) for (unsigned op : {0x923b, 0x923c, 0x923e, 0x9250}) put(info, op, 2);
        for (int i = 0; i < 4; ++i) put(info, 0, 4);
        for (auto s : {"Sony", "Simulated", "1", ""}) string(info, s);
        reply(1, info); reply(0); reply(1); reply(2);
        reply(3); // not ready on first attempt
        Buffer ext;
        put(ext, 300, 2); put(ext, 1, 4); put(ext, 0xd21d, 2); put(ext, body ? 5 : 3, 4);
        for (unsigned c : {0xd2c1, 0xd2c2, 0xd2c8}) put(ext, c, 2);
        if (body) for (unsigned c : {0xd309, 0xd30b}) put(ext, c, 2);
        reply(4, ext); reply(5); reply(6, dataset ? *dataset : props(recording));
    }
};
static void press(Buffer& out, unsigned tid, unsigned key, unsigned val) {
    Buffer req, start, end;
    put(req, 2, 4); put(req, 0x9207, 2); put(req, tid, 4); put(req, key, 4);
    packet(out, 6, req);
    put(start, tid, 4); put(start, 2, 8); packet(out, 9, start);
    put(end, tid, 4); put(end, val, 2); packet(out, 12, end);
}
static void fullRead(Buffer& out, unsigned tid) {
    Buffer request;
    put(request, 1, 4); put(request, 0x9209, 2); put(request, tid, 4); put(request, 0, 4);
    packet(out, 6, request);
}
static void simulated() {
    Peer p;
    uint8_t space[1024]{}, guid[16]{};
    Session s(p, space, sizeof space);
    p.prepare();
    CHECK(s.start(guid) == 0 && s.version() == 300 && !strcmp(s.model(), "Simulated"));
    CHECK(s.recordingRaw() == 0 && p.opened[0] && p.opened[1]);
    Buffer evt; packet(evt, 3, {37,0,0,0});
    CHECK(p.sent[1] == evt);
    packet(p.input[1], 13, {}); packet(p.input[1], 8, {3,0xc2,9,0,0,0,0x1d,0xd2,0,0});
    Event e{};
    CHECK(s.event(e) == 0 && s.event(e) == 1 && e.code == 0xc203 && e.params[0] == 0xd21d);
    packet(evt, 14, {}); CHECK(p.sent[1] == evt);
    p.reply(7, p.props(1)); CHECK(s.capture(0) == StateChanged);
    p.reply(8, p.props(0));
    p.reply(9); p.reply(10, p.props(0));
    for (int i = 11; i <= 13; ++i) p.reply(i);
    CHECK(s.capture(0) == 0);
    Buffer keys;
    press(keys, 9, 0xd2c1, 2); fullRead(keys, 10); press(keys, 11, 0xd2c2, 2);
    press(keys, 12, 0xd2c2, 1); press(keys, 13, 0xd2c1, 1);
    CHECK(p.sent[0].size() >= keys.size() && std::equal(keys.rbegin(), keys.rend(), p.sent[0].rbegin()));
    p.reply(14, p.props(0)); p.reply(15); p.reply(16);
    CHECK(s.movie(0) == 0);
    p.reply(17, p.props(1)); p.reply(18); p.reply(19);
    CHECK(s.movie(1) == 0);
    keys.clear(); press(keys, 18, 0xd2c8, 2); press(keys, 19, 0xd2c8, 1);
    CHECK(std::equal(keys.rbegin(), keys.rend(), p.sent[0].rbegin()));
    p.reply(20, p.props(0)); p.reply(21); p.reply(22, p.props(0));
    p.reply(23, {}, 0x2019); p.reply(24); p.reply(25);
    CHECK(s.capture(0) == 0x2019);
    keys.clear(); press(keys, 24, 0xd2c2, 1); press(keys, 25, 0xd2c1, 1);
    CHECK(std::equal(keys.rbegin(), keys.rend(), p.sent[0].rbegin()));
    p.reply(26, p.props(0)); p.reply(27); p.reply(28, p.props(1)); p.reply(29);
    CHECK(s.capture(0) == StateChanged && s.recordingRaw() == 1);
    keys.clear(); press(keys, 27, 0xd2c1, 2); fullRead(keys, 28); press(keys, 29, 0xd2c1, 1);
    CHECK(std::equal(keys.rbegin(), keys.rend(), p.sent[0].rbegin()));
    p.reply(999, p.props(0)); CHECK(s.refresh() < 0 && !s.connected() && s.recordingRaw() == -1);
    size_t before = p.sent[0].size();
    s.close(); CHECK(p.sent[0].size() == before);
}
static void failedTransfers() {
    uint8_t scratch[512]{}, guid[16]{};
    Peer refused;
    packet(refused.input[0], 5, {2,0,0,0});
    Session first(refused, scratch, sizeof scratch);
    CHECK(first.start(guid) == InitRefused && first.refusalReason() == 2 && !refused.opened[1]);
    for (bool oversize : {false, true}) {
        Peer peer;
        peer.prepare();
        Session s(peer, scratch, sizeof scratch);
        CHECK(s.start(guid) == 0);
        Buffer start;
        put(start, 7, 4); put(start, oversize ? sizeof scratch + 1 : 12, 8);
        packet(peer.input[0], 9, start); // no data after StartData: simulated EOF
        CHECK(s.movie(0) < 0 && !s.connected() && s.recordingRaw() == -1 && s.lastOp() == 0x9209);
    }
    Peer peer;
    peer.prepare();
    Session s(peer, scratch, sizeof scratch);
    CHECK(s.start(guid) == 0);
    peer.reply(7, peer.props(0)); peer.reply(8); peer.reply(9, peer.props(0)); peer.reply(10);
    peer.reply(11, {}, 0x2019); peer.reply(12, {}, 0x2019); peer.reply(13);
    CHECK(s.capture(0) == 0x2019 && !s.connected());
    Buffer releases;
    press(releases, 11, 0xd2c2, 1); press(releases, 12, 0xd2c2, 1); press(releases, 13, 0xd2c1, 1);
    CHECK(std::equal(releases.rbegin(), releases.rend(), peer.sent[0].rbegin()));
}
static uint32_t timeMs;
static uint32_t clockMs() { return timeMs; }
static Buffer withParameters(Peer& peer, unsigned recording, unsigned iso, bool writable = true) {
    auto data = peer.props(recording);
    data[0] = 4;
    append(data, {0x1e,0xd2,6,0,uint8_t(writable),1});
    put(data, 100, 4); put(data, iso, 4);
    append(data, {2,2,0}); put(data, 100, 4); put(data, 400, 4);
    append(data, {1,0}); put(data, 800, 4); // reportable only
    append(data, {0x10,0x50,3,0,1,1,0,0,0x38,0xff,1,0x18,0xfc,0xe8,3,0xc8,0});
    append(data, {0x0a,0x50,4,0,1,2,1,0,1,0,2,1,0,1,0,0,0}); // display-only
    return data;
}
static void captureControls() {
    Peer p;
    uint8_t data[1024]{}, guid[16]{};
    Session s(p, data, sizeof data);
    p.prepare();
    CHECK(s.start(guid) == 0);
    timeMs = 100;
    CaptureControl control(s, {}, clockMs);
    p.reply(7, withParameters(p, 0, 100));
    CHECK(control.tick(true));
    auto iso = control.state().find(Parameter::Iso);
    CHECK(iso && iso->options() == 2 && iso->option(1) == 400 && iso->indexOf(800) == iso->options());
    auto ev = control.state().find(Parameter::Ev);
    CHECK(ev && ev->options() == 11 && ev->current == -200 && ev->option(0) == -1000);
    CHECK(control.state().find(Parameter::Focus)->options() == 0);
    CHECK(parameterText(Parameter::MovieRate, 6) == "25p");
    CHECK(parameterText(Parameter::MovieFormat, 0x08) == "XAVC S 4K");
    CHECK(parameterText(Parameter::PhotoFormat, 4) == "RAW+HEIF");
    CHECK(parameterText(Parameter::WhiteBalance, 0x8002) == "荧光灯 冷白色");
    CHECK(parameterText(Parameter::WhiteBalance, 0x8012) == "色温");
    CHECK(parameterText(Parameter::WhiteBalance, 0x8021) == "自定义 2");
    CHECK(parameterText(Parameter::WhiteBalance, 0x8030) == "水中自动");
    CHECK(parameterText(Parameter::WhiteBalance, 0x803f) == "白平衡选项 32831");
    CHECK(parameterText(Parameter::RawCompression, 1) == "已压缩");
    CHECK(parameterText(Parameter::RawCompression, 6) == "无损压缩");
    CHECK(parameterText(Parameter::RawCompression, 7) == "已压缩 (HQ)");
    CHECK(parameterText(Parameter::MovieSetting, 0x36) == "100M 4:2:2 10bit");
    CHECK(parameterText(Parameter::Shutter, 0x100fa) == "1/250");
    const auto sent = p.sent[0].size();
    control.execute({CaptureAction::Set, Parameter::Iso, 6, 800});
    CHECK(p.sent[0].size() == sent && control.state().result == StateChanged);

    p.reply(8, withParameters(p, 0, 100)); p.reply(9);
    control.execute({CaptureAction::Set, Parameter::Iso, 6, 400});
    CHECK(control.state().find(Parameter::Iso)->current == 100 && control.state().pending == CaptureAction::Set);
    Buffer expected, start, payload;
    put(start, 9, 4); put(start, 4, 8);
    put(payload, 9, 4); put(payload, 400, 4);
    packet(expected, 6, {2,0,0,0,5,0x92,9,0,0,0,0x1e,0xd2,0,0});
    packet(expected, 9, start); packet(expected, 12, payload);
    CHECK(std::equal(expected.rbegin(), expected.rend(), p.sent[0].rbegin()));
    p.reply(10, withParameters(p, 0, 100)); timeMs += 550;
    CHECK(control.tick(false) && control.state().pending == CaptureAction::Set);
    p.reply(11, withParameters(p, 0, 400)); timeMs += 550;
    CHECK(control.tick(false) && control.state().pending == CaptureAction::None && !control.state().result);
    CHECK(control.state().find(Parameter::Iso)->current == 400);

    p.reply(12, withParameters(p, 0, 400));
    p.reply(13, {}, 0x2019);
    p.reply(14, withParameters(p, 0, 400));
    control.execute({CaptureAction::Set, Parameter::Iso, 6, 100});
    CHECK(control.state().result == 0x2019 && control.state().find(Parameter::Iso)->current == 400);

    p.reply(15, withParameters(p, 0, 400));
    p.reply(16); p.reply(17);
    const auto sentAt = timeMs;
    control.execute({CaptureAction::Start});
    CHECK(control.state().locked() && control.state().recording == Recording::Idle);
    CHECK(!control.state().canRequest({CaptureAction::Start}));
    auto recordingEntry = [&](unsigned raw) {
        auto props = p.props(raw);
        return Buffer(props.begin() + 8, props.end());
    };
    p.reply(18, recordingEntry(0)); timeMs += 250;
    CHECK(control.tick(false) && control.state().locked() && control.state().recording == Recording::Idle);
    p.reply(19, recordingEntry(1)); timeMs += 250;
    CHECK(control.tick(false) && control.state().recording == Recording::Active && control.state().since == sentAt);
    CHECK(control.state().elapsed(sentAt + 1499) == 0 && control.state().elapsed(sentAt + 1500) == 1);
    CHECK(!control.state().canRequest({CaptureAction::Photo}));
    p.reply(20, p.props(1)); p.reply(21); p.reply(22);
    control.execute({CaptureAction::Stop});
    CHECK(control.state().recording == Recording::Active && control.state().locked());
    p.reply(23, withParameters(p, 1, 400)); timeMs += 500;
    CHECK(control.tick(false) && control.state().recording == Recording::Active);
    p.reply(24, recordingEntry(0)); timeMs += 250;
    CHECK(control.tick(false) && !control.state().locked() && control.state().recording == Recording::Idle);

    packet(p.input[1], 8, {3,0xc2,0,0,0,0,0x1d,0xd2,0,0});
    p.reply(25, withParameters(p, 1, 400)); timeMs += 500;
    CHECK(control.tick(false) && !control.state().localStart && control.state().since == timeMs);
    packet(p.input[1], 8, {3,0xc2,0,0,0,0,0x1e,0xd2,0,0});
    CHECK(control.tick(false));
    p.reply(26, withParameters(p, 1, 800, false)); timeMs += 350;
    CHECK(control.tick(false) && control.state().find(Parameter::Iso)->current == 800);
    CHECK(!control.state().find(Parameter::Iso)->options());
    packet(p.input[1], 8, {0}); // broken event channel
    CHECK(!control.tick(false));
    auto sentAfterLoss = p.sent[0].size();
    s.close();
    CHECK(p.sent[0].size() == sentAfterLoss);
    Peer reconnected;
    reconnected.prepare(1);
    Session fresh(reconnected, data, sizeof data);
    CHECK(fresh.start(guid) == 0);
    CaptureControl resumed(fresh, {}, clockMs);
    CHECK(resumed.state().recording == Recording::Active && !resumed.state().localStart);

    const auto beforeFallback = reconnected.sent[0].size();
    reconnected.reply(7, {}, 0x2005);
    reconnected.reply(8, reconnected.props(0));
    timeMs += 1000;
    CHECK(resumed.tick(false) && !resumed.state().locked());
    CHECK(reconnected.sent[0].size() == beforeFallback + 44); // exactly two read requests
}
static void control32(Buffer& out, unsigned tid, unsigned code, uint32_t value) {
    Buffer req, start, end;
    put(req, 2, 4); put(req, 0x9207, 2); put(req, tid, 4); put(req, code, 4);
    packet(out, 6, req);
    put(start, tid, 4); put(start, 4, 8); packet(out, 9, start);
    put(end, tid, 4); put(end, value, 4); packet(out, 12, end);
}
static void idList(Buffer& b, unsigned code, std::initializer_list<unsigned> ids, unsigned enabled = 1) {
    put(b, code, 2); put(b, 4, 2); b.push_back(0); b.push_back(uint8_t(enabled));
    put(b, 0, 2); put(b, 0, 2); b.push_back(2);
    for (int list = 0; list < 2; ++list) { put(b, ids.size(), 2); for (auto id : ids) put(b, id, 2); }
}
static Buffer bodyDataset(unsigned enabled = 1) {
    Buffer data;
    put(data, 3, 8); append(data, {0x1d,0xd2,2,0,0,2,0,0,2,0,0,3,0,0,1,2});
    idList(data, 0xd208, {1, 5, 6, 0x1a}, enabled);
    idList(data, 0xd20a, {0x4001, 0x4002}, enabled);
    return data;
}
static void bodyControls() {
    uint8_t space[2048]{}, guid[16]{};
    {
        Peer plain;
        auto dataset = bodyDataset();
        plain.prepare(0, false, &dataset);
        Session s(plain, space, sizeof space);
        const uint16_t* ids;
        CHECK(s.start(guid) == 0 && s.buttons(ids) == 0 && s.dials(ids) == 0);
        const auto before = plain.sent[0].size();
        CHECK(s.pressButton(5) == Unsupported && s.turnDial(0x4001, 1) == Unsupported && plain.sent[0].size() == before);
    }
    {
        Peer disabled;
        auto dataset = bodyDataset(0);
        disabled.prepare(0, true, &dataset);
        Session s(disabled, space, sizeof space);
        const uint16_t* ids;
        CHECK(s.start(guid) == 0 && s.buttons(ids) == 0 && s.dials(ids) == 0 && !s.hasButton(5));
    }
    Peer p;
    auto dataset = bodyDataset();
    p.prepare(0, true, &dataset);
    Session s(p, space, sizeof space);
    CHECK(s.start(guid) == 0);
    const uint16_t* ids;
    CHECK(s.buttons(ids) == 4 && ids[0] == 1 && ids[1] == 5 && ids[2] == 6 && ids[3] == 0x1a);
    CHECK(s.dials(ids) == 2 && ids[0] == 0x4001 && ids[1] == 0x4002);
    CHECK(s.hasButton(5) && !s.hasButton(2) && s.hasDial(0x4002) && !s.hasDial(0x4003));

    auto before = p.sent[0].size();
    CHECK(s.pressButton(2) == Unsupported && s.turnDial(0x4003, 1) == Unsupported && p.sent[0].size() == before);
    CHECK(s.turnDial(0x4001, 0) == 0 && p.sent[0].size() == before);

    p.reply(7); p.reply(8);
    CHECK(s.pressButton(5) == 0);
    Buffer expected;
    control32(expected, 7, 0xd309, 5u << 16 | 2); control32(expected, 8, 0xd309, 5u << 16 | 1);
    CHECK(p.sent[0].size() == before + expected.size() &&
          std::equal(expected.begin(), expected.end(), p.sent[0].begin() + before));

    p.reply(9, {}, 0x2019); p.reply(10);
    CHECK(s.pressButton(6) == 0x2019 && s.connected());
    expected.clear();
    control32(expected, 9, 0xd309, 6u << 16 | 2); control32(expected, 10, 0xd309, 6u << 16 | 1);
    CHECK(std::equal(expected.rbegin(), expected.rend(), p.sent[0].rbegin()));

    p.reply(11); p.reply(12);
    CHECK(s.turnDial(0x4001, -1) == 0 && s.turnDial(0x4002, 3) == 0);
    expected.clear();
    control32(expected, 11, 0xd30b, 0x4001ffff); control32(expected, 12, 0xd30b, 0x40020003);
    CHECK(std::equal(expected.rbegin(), expected.rend(), p.sent[0].rbegin()));

    p.reply(13); p.reply(14, {}, 0x2019); p.reply(15, {}, 0x2019);
    CHECK(s.pressButton(1) == 0x2019 && !s.connected());
    expected.clear();
    control32(expected, 13, 0xd309, 1u << 16 | 2);
    control32(expected, 14, 0xd309, 1u << 16 | 1); control32(expected, 15, 0xd309, 1u << 16 | 1);
    CHECK(std::equal(expected.rbegin(), expected.rend(), p.sent[0].rbegin()));
    before = p.sent[0].size();
    CHECK(s.pressButton(5) == NotReady && p.sent[0].size() == before);
}
static void bodyQueue() {
    BodyQueue q;
    using K = BodyCommand;
    CHECK(q.push({K::Button, 5, 0, 0}));
    CHECK(q.push({K::Dial, 0x4001, 1, 10}) && q.push({K::Dial, 0x4001, 1, 20}) && q.size() == 2);
    CHECK(q.push({K::Dial, 0x4002, -1, 25}) && q.size() == 3);
    CHECK(q.push({K::Dial, 0x4002, 1, 30}) && q.size() == 2); // net zero turn is dropped
    CHECK(q.push({K::Button, 6, 0, 40}) && q.push({K::Dial, 0x4001, 2, 50}));
    auto a = q.pop(100), b = q.pop(100), c = q.pop(100), d = q.pop(100);
    CHECK(a && a->kind == K::Button && a->id == 5);
    CHECK(b && b->kind == K::Dial && b->id == 0x4001 && b->steps == 2 && b->at == 20);
    CHECK(c && c->kind == K::Button && c->id == 6);
    CHECK(d && d->id == 0x4001 && d->steps == 2 && !q.pop(100));
    for (unsigned i = 0; i < BodyQueue::capacity; ++i) CHECK(q.push({K::Button, uint16_t(1 + i % 4), 0, 0}));
    CHECK(!q.push({K::Button, 5, 0, 0}) && q.size() == BodyQueue::capacity);
    q.clear();
    CHECK(q.push({K::Button, 5, 0, 0}) && q.push({K::Button, 6, 0, 1000}));
    auto fresh = q.pop(BodyQueue::staleMs + 1);
    CHECK(fresh && fresh->id == 6 && !q.size());
    CHECK(!bodyKeyAllowed(0x1a) && bodyKeyAllowed(0x1c) && bodyDialAllowed(0x4003) && !bodyDialAllowed(0x4004));
}
static int turn(WheelGesture& g, int radius, int from, int to) {
    int steps = 0;
    const int stride = to > from ? 3 : -3;
    for (int a = from; a != to; ) {
        a = std::abs(to - a) < 3 ? to : a + stride;
        const double r = a * 3.14159265358979 / 180;
        steps += g.move(int(std::lround(radius * std::cos(r))), int(std::lround(radius * std::sin(r))));
    }
    return steps;
}
static void wheelGesture() {
    WheelGesture g;
    struct Tap { int x, y; WheelTap expected; } taps[] = {
        {0, -80, WheelTap::Up}, {2, 80, WheelTap::Down}, {-80, 3, WheelTap::Left}, {80, 0, WheelTap::Right},
        {5, 5, WheelTap::Set}, {60, -60, WheelTap::Right}, {-60, -61, WheelTap::Up},
    };
    for (const auto& t : taps) {
        CHECK(g.press(t.x, t.y) && g.move(t.x + 3, t.y - 2) == 0);
        CHECK(g.release(t.x + 3, t.y - 2) == t.expected);
    }
    CHECK(!g.press(0, -120) && g.release(0, -120) == WheelTap::None);
    CHECK(g.press(0, -80) && turn(g, 80, -90, -40) == 3 && g.release(61, -51) == WheelTap::None);
    CHECK(g.press(0, -80) && turn(g, 80, -90, -125) == -2 && g.release(-46, -66) == WheelTap::None);
    CHECK(g.press(-79, 14) && turn(g, 80, 170, 205) == 2); // crosses the ±180° seam clockwise
    g.cancel();
    CHECK(g.press(0, -80) && turn(g, 80, -90, -96) == 0 && turn(g, 80, -96, -90) == 0 && g.moved());
    CHECK(g.release(0, -80) == WheelTap::None); // a 12° wobble is a turn attempt, not a tap
    CHECK(g.press(0, -80) && turn(g, 80, -90, -86) == 0 && g.release(5, -80) == WheelTap::Up);
    CHECK(g.press(3, 3) && g.move(40, 3) == 0 && g.release(40, 3) == WheelTap::None);
    CHECK(g.press(0, -90) && g.move(0, -60) == 0 && g.release(0, -60) == WheelTap::None);
    CHECK(g.press(0, -80) && turn(g, 80, -90, 275) == 24 && g.release(0, -80) == WheelTap::None);
}
namespace {
constexpr uint64_t shot = 1795615200000;
Buffer dates(unsigned count = 1) {
    Buffer data;
    put(data, count, 4);
    while (count--) put(data, shot, 8);
    return data;
}
Buffer contents(unsigned count = 1, uint64_t created = shot, bool paired = true, bool video = false) {
    Buffer data;
    put(data, 100, 4);
    data.resize(0x90);
    put(data, count, 4);
    for (unsigned i = 0; i < count; ++i) {
        for (unsigned value : {video ? 4u : 1u, 0x20001u + i, 100u, 1u, 0u, 0u, 0u}) put(data, value, 4);
        for (auto value : {created + i, created + i + 5000, created + i + 28800000, created + i + 28805000})
            put(data, value, 8);
        for (unsigned value : {0, 0, 0, 0, paired ? 2 : 1}) put(data, value, 4);
        auto file = [&](unsigned id, const char* path, unsigned format) {
            put(data, id, 4);
            put(data, strlen(path) + 1, 4);
            data.insert(data.end(), path, path + strlen(path) + 1);
            put(data, format, 4);
            put(data, 3500000, 8);
            data.resize(data.size() + 32, uint8_t(id));
            put(data, 0, 4);
            put(data, format == 0xb982, 4);
            if (format == 0xb982) data.resize(data.size() + 0x4c);
            put(data, 0, 4);
        };
        if (paired) file(11, "A:/DCIM/DSC00001.ARW", 0xb101);
        file(29, video ? "A:/M4ROOT/C0001.MP4" : "A:/DCIM/DSC00001.JPG", video ? 0xb982 : 0x3801);
    }
    return data;
}
void checkRequest(const Peer& peer, unsigned op, unsigned tid, std::initializer_list<uint32_t> parameters) {
    Buffer request, expected;
    put(request, 1, 4); put(request, op, 2); put(request, tid, 4);
    for (const auto p : parameters) put(request, p, 4);
    packet(expected, 6, request);
    CHECK(expected.size() <= peer.sent[0].size() &&
          std::equal(expected.rbegin(), expected.rend(), peer.sent[0].rbegin()));
}
Buffer autoProperties(Peer& peer, unsigned iso = 100, unsigned drive = 1, unsigned mode = 1) {
    auto data = withParameters(peer, 0, iso);
    data[0] += 2;
    for (auto field : {std::pair<unsigned, unsigned>{0x5013, drive}, {0x500e, mode}}) {
        put(data, field.first, 2);
        append(data, {6, 0, 0, 1});
        put(data, 1, 4); put(data, field.second, 4);
        data.push_back(0);
    }
    return data;
}
bool autoPermit(void* value) { return *static_cast<bool*>(value); }

void intervalControls() {
    for (auto scenario : {AutoResult::Done, AutoResult::Stopped, AutoResult::Failed}) {
        Peer peer;
        const auto properties = autoProperties(peer);
        peer.prepare(0, false, &properties, true);
        std::vector<uint8_t> scratch(64 * 1024);
        uint8_t guid[16]{};
        Session session(peer, scratch.data(), scratch.size());
        Parameters initial;
        CHECK(!session.start(guid, collectParameter, &initial));
        timeMs = UINT32_MAX - 250;
        CaptureControl capture(session, std::move(initial), clockMs);
        Automation automation(session, capture, clockMs, nullptr, {});
        bool permitted = true;
        auto step = [&] { return automation.tick(autoPermit, &permitted); };
        automation.start(AutoAction::Interval, {1, 2});
        peer.reply(7, dates()); CHECK(!step());
        peer.reply(8, contents()); CHECK(!step());
        peer.reply(9, dates(0)); CHECK(!step());
        peer.reply(10, properties); peer.reply(11);
        if (scenario == AutoResult::Stopped) {
            peer.cancelOnHalfPress = &permitted;
            peer.reply(12);
            CHECK(step() && automation.state().result == AutoResult::Stopped && !automation.state().completed);
            Buffer released;
            press(released, 12, 0xd2c1, 1);
            CHECK(std::equal(released.rbegin(), released.rend(), peer.sent[0].rbegin()));
            const auto size = peer.sent[0].size();
            CHECK(!step() && size == peer.sent[0].size());
            continue;
        }
        peer.reply(12, properties);
        for (unsigned tid = 13; tid <= 15; ++tid) peer.reply(tid);
        CHECK(!step() && !automation.state().completed);
        peer.reply(16, contents()); CHECK(!step() && !automation.state().completed);
        peer.reply(17, contents(0)); CHECK(!step());
        const auto beforeWait = peer.sent[0].size();
        timeMs += 499;
        CHECK(!step() && peer.sent[0].size() == beforeWait);
        if (scenario == AutoResult::Failed) {
            timeMs += 120000;
            CHECK(step() && automation.state().error == ShotUnconfirmed && !automation.state().completed);
            CHECK(!step() && peer.sent[0].size() == beforeWait);
            continue;
        }
        ++timeMs;
        peer.reply(18, contents(2)); // existing photo plus one RAW + JPEG object
        CHECK(step() && automation.state().completed == 1);
        checkRequest(peer, 0x923c, 18, {uint32_t(shot - 1), uint32_t((shot - 1) >> 32), 100, 1, 0});
        timeMs += 499;
        const auto beforeDue = peer.sent[0].size();
        CHECK(!step() && peer.sent[0].size() == beforeDue);
        ++timeMs;
        peer.reply(19, contents(2)); CHECK(!step()); // refresh the watermark before the next release
        peer.reply(20, properties); peer.reply(21); peer.reply(22, properties);
        for (unsigned tid = 23; tid <= 25; ++tid) peer.reply(tid);
        CHECK(!step() && automation.state().completed == 1);
        peer.reply(26, contents(3));
        CHECK(step() && automation.state().result == AutoResult::Done && automation.state().completed == 2);
        CHECK(!step());
    }
}

void presetControls() {
    Peer peer;
    const auto initialProps = autoProperties(peer, 100);
    peer.prepare(0, false, &initialProps);
    uint8_t guid[16]{}, scratch[2048]{};
    Session session(peer, scratch, sizeof scratch);
    Parameters initial;
    CHECK(!session.start(guid, collectParameter, &initial));
    timeMs = 100;
    CaptureControl capture(session, std::move(initial), clockMs);
    static Preset saved;
    Automation automation(session, capture, clockMs, [](const Preset& preset) { saved = preset; return true; }, {});
    bool permitted = true;
    auto step = [&] { return automation.tick(autoPermit, &permitted); };
    const auto onConnect = peer.sent[0].size();
    CHECK(!step() && peer.sent[0].size() == onConnect);
    automation.start(AutoAction::SavePreset);
    peer.reply(7, autoProperties(peer, 400));
    CHECK(step() && automation.state().result == AutoResult::Done);
    checkRequest(peer, 0x9209, 7, {0});
    CHECK(saved.validFor("Simulated") && !saved.validFor("ILCE-7M5") && saved.count == 2);
    CHECK(saved.entries[0].id == Parameter::Iso && saved.entries[0].value == 400);
    CHECK(saved.entries[1].id == Parameter::Ev && saved.entries[1].value == -200);
    auto blob = std::make_shared<Preset>(saved);
    blob->entries[1].value = 999; // off-step, cannot be written
    Automation restored(session, capture, clockMs, nullptr, blob);
    auto apply = [&] { return restored.tick(autoPermit, &permitted); };
    const auto afterSave = peer.sent[0].size();
    CHECK(!apply() && peer.sent[0].size() == afterSave);
    restored.start(AutoAction::ApplyPreset);
    peer.reply(8, autoProperties(peer, 100, 1, 2));
    CHECK(apply() && restored.state().result == AutoResult::Failed);
    checkRequest(peer, 0x9209, 8, {0});

    restored.start(AutoAction::ApplyPreset);
    peer.reply(9, initialProps); peer.reply(10, initialProps); peer.reply(11);
    CHECK(!apply() && !restored.state().completed && session.lastOp() == 0x9205);
    CHECK(!apply());
    peer.reply(12, autoProperties(peer, 400));
    CHECK(capture.tick(true) && apply() && restored.state().completed == 1);
    peer.reply(13, autoProperties(peer, 400));
    CHECK(apply() && restored.state().result == AutoResult::Done &&
          restored.state().completed == 1 && restored.state().total == 2);
    checkRequest(peer, 0x9209, 13, {0});
}
void mediaControls() {
    Peer peer;
    peer.prepare(0, false, nullptr, true);
    std::vector<uint8_t> scratch(64 * 1024);
    uint8_t guid[16]{};
    Session session(peer, scratch.data(), scratch.size());
    CHECK(!session.start(guid) && session.supportsMedia() && session.supportsMediaDelete());
    MediaWorker worker;
    worker.begin();
    CHECK(worker.hasWork({}));
    const MediaWindow empty{};
    auto permit = [](void*) { return true; };
    auto step = [&](const MediaWindow& window = {}) { return worker.step(session, window, permit, nullptr); };
    const auto newer = shot + 30000;
    const auto raw = contents(1, newer);
    peer.reply(7, dates()); CHECK(!step()); checkRequest(peer, 0x923b, 7, {1});
    peer.reply(8, contents()); CHECK(!step()); checkRequest(peer, 0x923c, 8, {0, 0, 100, 1, 0});
    peer.reply(9, dates()); CHECK(!step());
    peer.reply(10, raw); CHECK(step());
    auto items = worker.state().items;
    CHECK(items && items->size() == 2 && (*items)[0].key.slot == 2 && (*items)[1].key.slot == 1);
    CHECK((*items)[0].key.id == (*items)[1].key.id && (*items)[0].files == (*items)[1].files);
    CHECK((*items)[0].files.size() == 2 && !strcmp((*items)[0].files[0].name(), "DSC00001.JPG") &&
          (*items)[0].files[1].type() == "RAW");
    CHECK(mediaAge((*items)[0], newer + 30000) == "30秒前拍摄");
    CHECK(mediaAge((*items)[0], newer + 60000) == "1分钟前拍摄");
    CHECK(mediaAge((*items)[0], newer + 3600000) == "1小时前拍摄");
    CHECK(mediaAge((*items)[0], newer + 86400000) == "1天前拍摄");
    CHECK(mediaAge((*items)[0], newer - 1).empty() && mediaAge((*items)[0], 0).empty());
    CHECK(mediaDate((*items)[0]) == "2026/11/25 22:00:30");
    MediaWindow window{};
    window[0] = (*items)[0].key;
    CHECK(worker.hasWork(window));
    peer.reply(11, {0,0,0,0});
    CHECK(step(window) && !worker.state().tiles[0].image.pixels);
    checkRequest(peer, 0x923e, 11, {0x20001, 0x0200001d, 1});
    CHECK(!step(window));
    CHECK(!worker.hasWork(window));
    // RAW disappeared from the same Content ID. Neither name nor ID alone authorizes deletion.
    worker.erase(std::shared_ptr<const MediaItem>(items, &items->front()));
    CHECK(!worker.event({0xc240, 0, {1, 0x20001, 1}, 3}));
    peer.reply(12, dates()); CHECK(!step());
    peer.reply(13, contents(1, newer, false)); CHECK(step());
    CHECK(worker.state().status == MediaState::DeleteFailed && worker.state().error == StateChanged);
    CHECK(session.lastOp() == 0x923c);
    worker.erase(std::shared_ptr<const MediaItem>(items, &items->front()));
    peer.reply(14, dates()); CHECK(!step());
    peer.reply(15, raw); CHECK(!step());
    CHECK(worker.step(session, empty, [](void*) { return false; }, nullptr));
    CHECK(session.lastOp() == 0x923c && worker.state().status == MediaState::DeleteFailed);
    worker.erase(std::shared_ptr<const MediaItem>(items, &items->front()));
    peer.reply(16, dates()); CHECK(!step());
    peer.reply(17, raw); CHECK(!step());
    peer.reply(18, {}, 0x2019); CHECK(step());
    CHECK(worker.state().status == MediaState::DeleteFailed && worker.state().error == 0x2019);
    CHECK(worker.state().items == items);
    checkRequest(peer, 0x9250, 18, {0x20001, 2});
    worker.erase(std::shared_ptr<const MediaItem>(items, &items->front()));
    peer.reply(19, dates()); CHECK(!step());
    peer.reply(20, raw); CHECK(!step());
    peer.reply(21); CHECK(step());
    CHECK(worker.state().status == MediaState::Deleting);
    checkRequest(peer, 0x9250, 21, {0x20001, 2});
    worker.invalidate(); // contents changed notification, still require a directory readback
    peer.reply(22, dates()); CHECK(!step());
    peer.reply(23, contents()); CHECK(!step());
    peer.reply(24, dates(0)); CHECK(step());
    CHECK(worker.state().status == MediaState::Deleted && worker.state().items->size() == 1 &&
          worker.state().items->front().key.slot == 1 && !worker.state().tiles[0].key.slot);
    worker.invalidate();
    peer.reply(25, {}, 0x201d); CHECK(!step());
    peer.reply(26, {}, 0x2013); CHECK(step());
    CHECK(worker.state().status == MediaState::Ready && worker.state().items->empty());
    peer.reply(27, Buffer(65 * 1024, 0xff));
    Bytes thumbnail{};
    CHECK(session.mediaThumbnail(1, 0x20001, 29, thumbnail) == Unsupported && session.connected());
    worker.invalidate();
    peer.reply(28, dates()); CHECK(!step());
    peer.reply(29, contents(100)); CHECK(!step());
    peer.reply(30, contents(100)); CHECK(step()); // repeated timestamp cursor must terminate
    checkRequest(peer, 0x923c, 30, {uint32_t(shot + 99), uint32_t((shot + 99) >> 32), 100, 1, 0});
    CHECK(worker.state().status == MediaState::ListFailed);
    // A disappeared card is ambiguous after a successful reply. Only the
    // matching slot + content event can distinguish it from an empty slot.
    for (bool matching : {false, true}) {
        const unsigned tid = matching ? 36 : 31;
        worker.erase(std::shared_ptr<const MediaItem>(items, &items->front()));
        peer.reply(tid, dates()); CHECK(!step());
        peer.reply(tid + 1, raw); CHECK(!step());
        peer.reply(tid + 2); CHECK(step());
        CHECK(worker.event({0xc240, 0, {matching ? 2u : 1u, 0x20001, 1}, 3}));
        peer.reply(tid + 3, dates(0)); CHECK(!step());
        peer.reply(tid + 4, {}, 0x201d); CHECK(step());
        CHECK(worker.state().status == (matching ? MediaState::Deleted : MediaState::Unconfirmed));
    }
    MediaList list;
    const auto video = contents(1, shot, false, true);
    CHECK(parseMediaPage({video.data(), video.size()}, 1, list) && list.front().video());
    CHECK(mediaDate(list.front()) == "2026/11/25 22:00:00");
    CHECK(mediaAge(list.front(), newer) == "30秒前创建");
    CHECK(mediaAge(list.front(), shot - 1).empty());
    list.front().local = 0;
    CHECK(mediaDate(list.front()) == "2026/11/25 14:00:00 UTC");
    list.front().created = 0;
    CHECK(mediaDate(list.front()).empty() && mediaAge(list.front(), newer).empty());
    for (size_t n = 0; n < raw.size(); ++n) CHECK(!parseMediaPage({raw.data(), n}, 1, list));
}
}
int main(int argc, char** argv) {
    intervalControls();
    presetControls();
    simulated();
    failedTransfers();
    captureControls();
    bodyControls();
    bodyQueue();
    wheelGesture();
    mediaControls();
    Peer peer;
    auto p = peer.props(3);
    CHECK(properties({p.data(),p.size()}, nullptr, nullptr));
    for (size_t n = 0; n < p.size(); ++n) CHECK(!properties({p.data(),n}, nullptr, nullptr));
    p[0] = 0xff; CHECK(!properties({p.data(),p.size()}, nullptr, nullptr));
    if (argc == 2) {
        std::ifstream f(argv[1], std::ios::binary);
        Buffer bytes{std::istreambuf_iterator<char>(f), {}};
        unsigned count = 0;
        CHECK(properties({bytes.data(), bytes.size()}, [](const Property& prop, void* ctx) {
            ++*static_cast<unsigned*>(ctx);
            if (prop.code == 0xd21d) std::printf("fixture D21D type=%u enabled=%u current=%u\n",
                                                   prop.type, prop.enabled, prop.current.data[0]);
        }, &count));
        std::printf("fixture: %u descriptors\n", count);
        Peer upstream;
        upstream.prepare(0, true, &bytes);
        std::vector<uint8_t> scratch(64 * 1024);
        uint8_t guid[16]{};
        Session s(upstream, scratch.data(), scratch.size());
        CHECK(s.start(guid) == 0);
        const uint16_t* ids;
        const size_t buttons = s.buttons(ids);
        std::printf("fixture D208 buttons:");
        for (size_t i = 0; i < buttons; ++i) std::printf(" 0x%X", ids[i]);
        const size_t dials = s.dials(ids);
        std::printf("\nfixture D20A dials:");
        for (size_t i = 0; i < dials; ++i) std::printf(" 0x%X", ids[i]);
        std::printf("\n");
        CHECK(buttons == 13 && dials == 3 && s.hasButton(0x1c) && s.hasDial(0x4003));
    }
    std::puts("Sony PTP/IP simulation passed");
}
