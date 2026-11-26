// Wire layouts and button sequences adapted from alpha2go (Apache-2.0).
// See licenses/alpha2go.txt. This implementation bounds transfers and runs on one worker.
#include "session.h"
#include <cstring>

namespace sony {
namespace {
uint64_t number(const uint8_t* p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= uint64_t(p[i]) << (8 * i);
    return v;
}
void put(uint8_t* p, uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) { p[i] = uint8_t(v); v >>= 8; }
}
struct Reader {
    Bytes bytes;
    size_t pos = 0;
    bool ok = true;
    Bytes take(size_t n) {
        if (n > bytes.size - pos) { ok = false; return {nullptr, 0}; }
        Bytes b{bytes.data + pos, n};
        pos += n;
        return b;
    }
    uint64_t get(size_t n) { auto b = take(n); return number(b.data, b.size); }
    Bytes items(uint64_t count, size_t width) {
        if (count > (bytes.size - pos) / width) { ok = false; return {nullptr, 0}; }
        return take(size_t(count) * width);
    }
    Bytes string() { return items(get(1), 2); }
    Bytes array16() { return items(get(4), 2); }
    Bytes value(uint16_t type) {
        size_t begin = pos;
        if (type == 0xffff) string();
        else {
            bool array = (type & 0x4000) != 0;
            unsigned scalar = type & ~0x4000;
            if (!scalar || scalar > 10) { ok = false; return {nullptr, 0}; }
            items(array ? get(4) : 1, 1u << ((scalar - 1) / 2));
        }
        return {bytes.data + begin, pos - begin};
    }
};
bool has(Bytes list, uint16_t code) {
    for (size_t i = 0; i < list.size; i += 2)
        if (number(list.data + i, 2) == code) return true;
    return false;
}
bool deviceModel(Bytes data, char* name, size_t capacity, bool& media, bool& mediaDelete, bool& thumbnail) {
    Reader r{data};
    r.take(8); r.string(); r.take(2);
    auto ops = r.array16();
    for (int i = 0; i < 4; ++i) r.array16();
    r.string(); // manufacturer
    auto model = r.string();
    r.string(); r.string(); // firmware and serial are not logged
    if (!r.ok || model.size < 4 || !has(ops, 0x9201)) return false;
    media = has(ops, 0x923b) && has(ops, 0x923c);
    mediaDelete = has(ops, 0x9250);
    thumbnail = has(ops, 0x923e);
    size_t n = 0;
    for (; n + 1 < capacity && n * 2 < model.size; ++n) {
        auto c = number(model.data + n * 2, 2);
        if (!c) break;
        name[n] = c >= 32 && c < 127 ? char(c) : '?';
    }
    name[n] = 0;
    return n != 0;
}
Property readProperty(Reader& r) {
    Property p{};
    p.code = r.get(2); p.type = r.get(2);
    p.writable = r.get(1); p.enabled = r.get(1);
    r.value(p.type); p.current = r.value(p.type);
    p.form = r.get(1);
    size_t start = r.pos;
    if (p.form == 1) {
        for (int j = 0; j < 3; ++j) r.value(p.type);
    } else if (p.form == 2) {
        for (int j = 0; j < 2; ++j) {
            auto n = r.get(2);
            while (n-- && r.ok) r.value(p.type);
        }
    } else if (p.form != 0) r.ok = false;
    p.constraints = {r.bytes.data + start, r.pos - start};
    return p;
}
size_t settable(const Property& p, uint16_t* out, size_t capacity) {
    if (p.form != 2 || !p.enabled || p.type < 1 || p.type > 6) return 0;
    const size_t width = 1u << ((p.type - 1) / 2);
    const auto* list = p.constraints.data;
    const size_t count = number(list, 2);
    size_t n = 0;
    for (size_t i = 0; i < count && n < capacity; ++i) {
        const auto id = number(list + 2 + i * width, width);
        if (id <= 0xffff) out[n++] = uint16_t(id);
    }
    return n;
}
bool listed(const uint16_t* ids, size_t count, uint16_t id) {
    for (size_t i = 0; i < count; ++i) if (ids[i] == id) return true;
    return false;
}
int rawRecording(const Property& p) {
    return p.code == recordingProperty && p.type == 2 && p.enabled != 0 ? p.current.data[0] : -1;
}
} // namespace

bool properties(Bytes data, Visitor visit, void* context) {
    Reader r{data};
    uint64_t count = r.get(8);
    if (count > (data.size - r.pos) / 9) return false;
    for (uint64_t i = 0; i < count && r.ok; ++i) {
        Property p = readProperty(r);
        if (r.ok && visit) visit(p, context);
    }
    return r.ok && r.pos == data.size;
}
int Session::fail(int error) { healthy_ = false; recording_ = -1; return error; }
bool Session::send(Stream s, uint32_t kind, const uint8_t* data, size_t n) {
    uint8_t h[8];
    put(h, n + 8, 4); put(h + 4, kind, 4);
    return wire_.write(s, h, sizeof h) && (!n || wire_.write(s, data, n));
}
bool Session::header(Stream s, uint32_t& kind, size_t& length, const uint8_t* first) {
    uint8_t h[8];
    if (first) h[0] = *first;
    size_t off = first ? 1 : 0;
    if (!wire_.read(s, h + off, sizeof h - off)) return false;
    uint32_t total = number(h, 4);
    if (total < 8) return false;
    kind = number(h + 4, 4); length = total - 8;
    return true;
}
int Session::transact(uint16_t op, std::initializer_list<uint32_t> params, Bytes value) {
    if (!healthy_) return NotReady;
    wire_.deadline();
    lastOp_ = op;
    auto s = Stream::Command;
    uint32_t id = op == 0x1002 ? (tid_ = 0) : ++tid_;
    uint8_t b[32]{};
    put(b, value.size ? 2 : 1, 4); put(b + 4, op, 2); put(b + 6, id, 4);
    size_t n = 10;
    for (auto p : params) { put(b + n, p, 4); n += 4; }
    if (!send(s, 6, b, n)) return fail(Io);
    if (value.size) {
        put(b, id, 4); put(b + 4, value.size, 8);
        if (!send(s, 9, b, 12)) return fail(Io);
        memcpy(b + 4, value.data, value.size);
        if (!send(s, 12, b, 4 + value.size)) return fail(Io);
    }
    size_ = 0;
    bool started = false, ended = false, discarded = false;
    uint64_t expected = 0;
    for (;;) {
        uint32_t k;
        if (!header(s, k, n)) return fail(Io);
        if (k == 13 && !n) { if (!send(s, 14, nullptr, 0)) return fail(Io); continue; }
        if (k == 7) {
            if (n < 6 || n > 26 || (n - 6) % 4 || !wire_.read(s, b, n) ||
                number(b + 2, 4) != id) return fail(Invalid);
            int code = number(b, 2);
            if (code != 0x2001) return code ? code : fail(Invalid);
            if (started && !ended) return fail(Invalid);
            return discarded ? Unsupported : 0;
        }
        if (k == 9 && !started && !value.size && n == 12) {
            if (!wire_.read(s, b, n) || number(b, 4) != id) return fail(Invalid);
            expected = number(b + 4, 8);
            if (expected != UINT64_MAX && expected > capacity_ &&
                (op != 0x923e || expected > 256 * 1024)) return fail(Invalid);
            started = true;
        } else if ((k == 10 || k == 12) && started && !ended && n >= 4) {
            if (!wire_.read(s, b, 4) || number(b, 4) != id) return fail(Invalid);
            if (discarded || size_ > capacity_ || n - 4 > capacity_ - size_) {
                if (op != 0x923e || n - 4 > 256 * 1024 - size_) return fail(Invalid);
                discarded = true;
                for (size_t remaining = n - 4; remaining;) {
                    const auto chunk = remaining < capacity_ ? remaining : capacity_;
                    if (!wire_.read(s, data_, chunk)) return fail(Io);
                    remaining -= chunk;
                }
            } else if (n > 4 && !wire_.read(s, data_ + size_, n - 4)) return fail(Io);
            size_ += n - 4; ended = k == 12;
            if (expected != UINT64_MAX && (size_ > expected || (ended && size_ != expected))) return fail(Invalid);
        } else return fail(Invalid);
    }
}
int Session::start(const uint8_t guid[16], Visitor visit, void* context) {
    if (healthy_ || !data_ || capacity_ < 256) return NotReady;
    tid_ = 0; opened_ = ready_ = false; recording_ = -1;
    refusal_ = propCount_ = ctlCount_ = buttonCount_ = dialCount_ = 0;
    buttonControl_ = dialControl_ = false;
    model_[0] = 0; lastOp_ = version_ = 0;
    auto cmd = Stream::Command, evt = Stream::Event;
    uint8_t init[40]{};
    memcpy(init, guid, 16);
    constexpr char name[] = "ToolBoxOS";
    for (size_t i = 0; i < sizeof name; ++i) init[16 + 2 * i] = name[i];
    put(init + 16 + 2 * sizeof name, 0x10000, 4);
    wire_.deadline();
    if (!wire_.open(cmd) || !send(cmd, 1, init, sizeof init)) return fail(Io);
    int rc = initAck(cmd, 2);
    if (rc) return rc;
    wire_.deadline();
    if (!wire_.open(evt) || !send(evt, 3, data_, 4)) return fail(Io);
    if ((rc = initAck(evt, 4))) return rc;
    healthy_ = true;
    rc = transact(0x1001, {});
    if (rc) return rc;
    if (!deviceModel({data_, size_}, model_, sizeof model_, media_, mediaDelete_, mediaThumbnail_)) return fail(Unsupported);
    if ((rc = transact(0x1002, {1}))) return rc;
    opened_ = true;
    for (unsigned phase : {1u, 2u}) if ((rc = transact(0x9201, {phase, 0, 0}))) return rc;
    for (unsigned attempt = 0;; ++attempt) {
        if ((rc = transact(0x9202, {300, 1}))) return rc;
        if (size_) {
            Reader r{{data_, size_}};
            version_ = r.get(2);
            auto props = r.array16(), controls = r.array16();
            if (!r.ok) return fail(Invalid);
            if (props.size || controls.size) {
                if (version_ != 300) return Unsupported;
                propCount_ = props.size / 2;
                ctlCount_ = controls.size / 2;
                half_ = has(controls, 0xd2c1); full_ = has(controls, 0xd2c2); movie_ = has(controls, 0xd2c8);
                buttonControl_ = has(controls, 0xd309); dialControl_ = has(controls, 0xd30b);
                break;
            }
        }
        if (attempt == 19) return NotReady;
        wire_.pause(100);
    }
    if ((rc = transact(0x9201, {3, 0, 0}))) return rc;
    ready_ = true;
    rc = refresh(visit, context);
    if (rc) ready_ = false;
    return rc;
}
int Session::initAck(Stream s, uint32_t wanted) {
    uint32_t kind; size_t n;
    if (!header(s, kind, n)) return fail(Io);
    if (n > capacity_) return fail(Invalid);
    if (n && !wire_.read(s, data_, n)) return fail(Io);
    if (kind == 5 && n == 4) {
        refusal_ = number(data_, 4);
        return fail(InitRefused);
    }
    if (kind != wanted) return fail(Invalid);
    if (s == Stream::Event) return n ? fail(Invalid) : 0;
    // connection ID, responder GUID, terminated UTF-16 name and PTP/IP 1.0.
    if (n < 26 || n % 2 || number(data_ + n - 6, 2) || number(data_ + n - 4, 4) != 0x10000)
        return fail(Invalid);
    return 0;
}
int Session::refresh(Visitor visit, void* context) {
    recording_ = -1;
    singleShot_ = bulb_ = false;
    if (!connected()) return NotReady;
    int rc = transact(0x9209, {0});
    if (rc) return rc;
    Bytes dataset{data_, size_};
    if (!properties(dataset, nullptr, nullptr)) return fail(Invalid);
    buttonCount_ = dialCount_ = 0;
    struct View { Visitor visit; void* context; Session& self; } view{visit, context, *this};
    properties(dataset, [](const Property& p, void* arg) {
        auto& v = *static_cast<View*>(arg);
        if (p.code == recordingProperty) v.self.recording_ = rawRecording(p);
        else if (p.code == 0x5013 && p.enabled && (p.type == 4 || p.type == 6))
            v.self.singleShot_ = number(p.current.data, p.current.size) == 1;
        else if (p.code == 0xd20d && p.enabled && p.type == 6)
            v.self.bulb_ = number(p.current.data, p.current.size) == 0;
        else if (p.code == 0xd208) v.self.buttonCount_ = settable(p, v.self.buttons_, sizeof v.self.buttons_ / 2);
        else if (p.code == 0xd20a) v.self.dialCount_ = settable(p, v.self.dials_, sizeof v.self.dials_ / 2);
        if (v.visit) v.visit(p, v.context);
    }, &view);
    return 0;
}
int Session::readRecording() {
    recording_ = -1;
    const int rc = transact(0x9251, {recordingProperty});
    if (rc) return rc;
    Reader r{{data_, size_}};
    const auto p = readProperty(r);
    if (!r.ok || r.pos != size_ || p.code != recordingProperty) return Unsupported;
    recording_ = rawRecording(p);
    return 0;
}
int Session::setScalar(uint16_t code, uint16_t type, int64_t value) {
    if (!connected()) return NotReady;
    if (type < 1 || type > 6) return Unsupported;
    const size_t width = 1u << ((type - 1) / 2);
    uint8_t data[4];
    put(data, uint64_t(value), width);
    return transact(0x9205, {code}, {data, width});
}
int Session::event(Event& result) {
    if (!connected()) return NotReady;
    uint8_t b[18];
    wire_.deadline();
    int got = wire_.tryEventByte(b[0]);
    if (!got) return 0;
    uint32_t kind; size_t n;
    auto s = Stream::Event;
    if (got < 0 || !header(s, kind, n, b)) return fail(Io);
    if (kind == 13 && !n) return send(s, 14, nullptr, 0) ? 0 : fail(Io);
    if (kind != 8 || n < 6 || n > sizeof b || (n - 6) % 4 || !wire_.read(s, b, n)) return fail(Invalid);
    result.code = number(b, 2); result.transaction = number(b + 2, 4);
    result.count = (n - 6) / 4;
    for (size_t i = 0; i < result.count; ++i) result.params[i] = number(b + 6 + 4 * i, 4);
    return 1;
}
int Session::control(uint16_t code, uint32_t value, size_t width) {
    uint8_t data[4];
    put(data, value, width);
    return transact(0x9207, {code}, {data, width});
}
bool Session::hasButton(uint16_t id) const {
    return connected() && buttonControl_ && listed(buttons_, buttonCount_, id);
}
bool Session::hasDial(uint16_t id) const {
    return connected() && dialControl_ && listed(dials_, dialCount_, id);
}
int Session::pressButton(uint16_t id) {
    if (!connected()) return NotReady;
    if (!hasButton(id)) return Unsupported;
    const uint32_t key = uint32_t(id) << 16;
    const int pressed = control(0xd309, key | 2, 4);
    int released = control(0xd309, key | 1, 4);
    if (released > 0) released = control(0xd309, key | 1, 4);
    return released ? fail(released) : pressed;
}
int Session::turnDial(uint16_t id, int16_t steps) {
    if (!connected()) return NotReady;
    if (!hasDial(id)) return Unsupported;
    if (!steps) return 0;
    return control(0xd30b, uint32_t(id) << 16 | uint16_t(steps), 4);
}
int Session::release(uint16_t code) {
    int rc = button(code, 1);
    // Retry a rejected release once while the same transaction stream is in sync.
    if (rc > 0) rc = button(code, 1);
    return rc;
}
int Session::checkState(uint8_t expected) {
    int rc = refresh();
    if (rc) return rc;
    return recording_ == expected ? 0 : StateChanged;
}
int Session::capture(uint8_t observedIdle, bool (*allow)(void*), void* context) {
    if (!supportsCapture()) return Unsupported;
    int rc = checkState(observedIdle);
    if (rc) return rc;
    if (allow && !allow(context)) return StateChanged;
    int fullRelease = 0;
    rc = button(0xd2c1, 2);
    if (!rc) {
        wire_.pause(500);
        rc = allow && !allow(context) ? StateChanged : checkState(observedIdle);
        if (!rc && allow && !allow(context)) rc = StateChanged;
        if (!rc) {
            rc = button(0xd2c2, 2);
            if (!rc) wire_.pause(35);
            fullRelease = release(0xd2c2);
        }
    }
    int halfRelease = release(0xd2c1);
    if (fullRelease || halfRelease) return fail(fullRelease ? fullRelease : halfRelease);
    return rc;
}
int Session::movie(uint8_t expectedState, void (*beforePress)(void*), void* context) {
    if (!supportsMovie()) return Unsupported;
    int rc = checkState(expectedState);
    if (rc) return rc;
    if (beforePress) beforePress(context);
    rc = button(0xd2c8, 2);
    int result = release(0xd2c8);
    return result ? fail(result) : rc;
}
void Session::close() {
    if (healthy_ && opened_) transact(0x1003, {});
    healthy_ = opened_ = ready_ = false;
    recording_ = -1;
}
} // namespace sony
