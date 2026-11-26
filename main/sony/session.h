#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace sony {
// One foreground worker owns both streams. Negative results are local failures,
// positive results preserve the camera's PTP response code.
enum Error { Io = -1, Invalid = -2, Unsupported = -3, StateChanged = -4, NotReady = -5, InitRefused = -6 };
enum class Stream { Command, Event };
struct Wire {
    virtual bool open(Stream) = 0;
    virtual void deadline() = 0;
    virtual bool read(Stream, void*, size_t) = 0;
    virtual bool write(Stream, const void*, size_t) = 0;
    virtual int tryEventByte(uint8_t&) = 0; // 0 idle, 1 byte, negative EOF/error
    virtual void pause(unsigned ms) = 0;
protected:
    ~Wire() = default;
};
struct Bytes { const uint8_t* data; size_t size; };
constexpr uint16_t recordingProperty = 0xd21d;
struct Property {
    uint16_t code, type;
    uint8_t writable, enabled, form;
    Bytes current, constraints; // wire values; enum contains settable then readable lists
};
using Visitor = void (*)(const Property&, void*);
bool properties(Bytes data, Visitor visit, void* context);
struct Event { uint16_t code; uint32_t transaction, params[3]; size_t count; };

class Session {
public:
    Session(Wire& wire, uint8_t* scratch, size_t capacity)
        : wire_(wire), data_(scratch), capacity_(capacity) {}
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    int start(const uint8_t guid[16], Visitor visit = nullptr, void* context = nullptr);
    int refresh(Visitor visit = nullptr, void* context = nullptr);
    int readRecording();
    int setScalar(uint16_t code, uint16_t type, int64_t value);
    int event(Event& event); // 1 event, 0 idle/probe, negative connection failure
    int capture(uint8_t observedIdle, bool (*allow)(void*) = nullptr, void* context = nullptr);
    bool singleShot() const { return singleShot_ && !bulb_; }
    int pressButton(uint16_t id); // 0xD309 press then release, release sent even if the press is refused
    int turnDial(uint16_t id, int16_t steps); // 0xD30B, positive is clockwise
    int movie(uint8_t expectedState, void (*beforePress)(void*) = nullptr, void* context = nullptr);
    void close();
    const char* model() const { return model_; }
    uint16_t version() const { return version_; }
    int recordingRaw() const { return recording_; }
    bool connected() const { return ready_ && healthy_; }
    uint16_t lastOp() const { return lastOp_; }
    uint32_t refusalReason() const { return refusal_; }
    size_t propertyCount() const { return propCount_; }
    size_t controlCount() const { return ctlCount_; }
    bool supportsCapture() const { return connected() && half_ && full_; }
    bool supportsMovie() const { return connected() && movie_; }
    bool hasButton(uint16_t id) const;
    bool hasDial(uint16_t id) const;
    int mediaDates(uint8_t slot, bool& hasDates);
    int mediaPage(uint8_t slot, uint64_t after, Bytes& reply);
    int mediaThumbnail(uint8_t slot, uint32_t id, uint16_t file, Bytes& reply);
    int deleteMedia(uint8_t slot, uint32_t id);
    bool supportsMedia() const { return connected() && media_; }
    bool supportsMediaDelete() const { return connected() && mediaDelete_; }
    bool supportsThumbnail() const { return connected() && mediaThumbnail_; }
    // Usable 0xD208 / 0xD20A IDs from the last full read; empty without the matching control.
    size_t buttons(const uint16_t*& ids) const { ids = buttons_; return connected() && buttonControl_ ? buttonCount_ : 0; }
    size_t dials(const uint16_t*& ids) const { ids = dials_; return connected() && dialControl_ ? dialCount_ : 0; }
private:
    Wire& wire_;
    uint8_t* data_;
    size_t capacity_, size_ = 0;
    bool healthy_ = false, opened_ = false, ready_ = false;
    bool half_ = false, full_ = false, movie_ = false, buttonControl_ = false, dialControl_ = false;
    bool singleShot_ = false, bulb_ = false;
    bool media_ = false, mediaDelete_ = false, mediaThumbnail_ = false;
    uint16_t buttons_[32]{}, dials_[8]{};
    size_t buttonCount_ = 0, dialCount_ = 0;
    char model_[128]{};
    uint16_t version_ = 0, lastOp_ = 0;
    int recording_ = -1;
    uint32_t tid_ = 0;
    uint32_t refusal_ = 0;
    size_t propCount_ = 0, ctlCount_ = 0;
    int fail(int error);
    bool send(Stream, uint32_t kind, const uint8_t*, size_t);
    bool header(Stream, uint32_t& kind, size_t& length, const uint8_t* first = nullptr);
    int initAck(Stream, uint32_t kind);
    int transact(uint16_t op, std::initializer_list<uint32_t> params, Bytes value = {});
    int control(uint16_t code, uint32_t value, size_t width);
    int button(uint16_t code, uint16_t value) { return control(code, value, 2); }
    int release(uint16_t code);
    int checkState(uint8_t expected);
};
} // namespace sony
