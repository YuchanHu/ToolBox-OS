#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sony {
// IDs as listed by 0xD208 / 0xD20A on the α6700 (alpha2go docs/protocol.md). Only IDs the
// connected camera lists are offered. MOVIE (0x1A) is deliberately absent: recording goes
// through the muted, touch-locked capture path only.
enum class BodyKey : uint16_t {
    Up = 1, Down = 2, Left = 3, Right = 4, Set = 5, Menu = 6, Fn = 0x10, Playback = 0x11,
    C1 = 0x14, C2 = 0x15, C3 = 0x16, AfOn = 0x1c
};
enum class BodyDial : uint16_t { ControlWheel = 0x4001, Front = 0x4002, Rear = 0x4003 };
bool bodyKeyAllowed(uint16_t id);
bool bodyDialAllowed(uint16_t id);

struct BodyCommand {
    enum Kind : uint8_t { Button, Dial } kind = Button;
    uint16_t id = 0;
    int16_t steps = 0;
    uint32_t at = 0;
};

// Serialises touch input for the worker: FIFO order, consecutive turns of one dial merge,
// and commands older than staleMs are dropped instead of replayed late.
class BodyQueue {
public:
    static constexpr size_t capacity = 8;
    static constexpr uint32_t staleMs = 1500;
    bool push(BodyCommand command);
    std::optional<BodyCommand> pop(uint32_t now);
    void clear() { size_ = 0; }
    size_t size() const { return size_; }
private:
    BodyCommand items_[capacity]{};
    size_t size_ = 0;
};

struct BodyState {
    std::vector<uint16_t> buttons, dials; // listed by the camera and offered by this app
    unsigned resultId = 0;
    int result = 0;
    bool has(BodyKey key) const;
    bool has(BodyDial dial) const;
    bool any() const { return !buttons.empty() || !dials.empty(); }
    bool operator==(const BodyState& other) const;
};

// Control-wheel touch interpretation in wheel-centred pixels (y grows downward, so a
// positive angle change is clockwise, matching the protocol's positive dial steps).
enum class WheelTap { None, Up, Down, Left, Right, Set };
class WheelGesture {
public:
    static constexpr int stepDegrees = 15, centerRadius = 46, outerRadius = 109;
    bool press(int x, int y);
    int move(int x, int y);
    WheelTap release(int x, int y);
    void cancel() { active_ = false; }
    bool active() const { return active_; }
    bool moved() const { return moved_; }
private:
    float last_ = 0, accumulated_ = 0, turned_ = 0;
    int startX_ = 0, startY_ = 0;
    bool active_ = false, ring_ = false, moved_ = false;
};
}
