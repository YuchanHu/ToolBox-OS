#include "body.h"
#include <algorithm>
#include <cmath>

namespace sony {
namespace {
constexpr uint16_t keys[] = {1, 2, 3, 4, 5, 6, 0x10, 0x11, 0x14, 0x15, 0x16, 0x1c};
constexpr uint16_t dials[] = {0x4001, 0x4002, 0x4003};
constexpr float pi = 3.14159265f;
float degrees(int x, int y) { return std::atan2(float(y), float(x)) * 180.0f / pi; }
bool contains(const std::vector<uint16_t>& list, uint16_t id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}
}
bool bodyKeyAllowed(uint16_t id) { return std::find(std::begin(keys), std::end(keys), id) != std::end(keys); }
bool bodyDialAllowed(uint16_t id) { return std::find(std::begin(dials), std::end(dials), id) != std::end(dials); }

bool BodyQueue::push(BodyCommand command) {
    if (command.kind == BodyCommand::Dial) {
        if (!command.steps) return true;
        if (size_) {
            auto& tail = items_[size_ - 1];
            if (tail.kind == BodyCommand::Dial && tail.id == command.id) {
                const int sum = std::clamp(int(tail.steps) + command.steps, -32767, 32767);
                if (!sum) { --size_; return true; }
                tail.steps = int16_t(sum);
                tail.at = command.at;
                return true;
            }
        }
    }
    if (size_ == capacity) return false;
    items_[size_++] = command;
    return true;
}
std::optional<BodyCommand> BodyQueue::pop(uint32_t now) {
    while (size_) {
        const auto first = items_[0];
        std::move(items_ + 1, items_ + size_, items_);
        --size_;
        if (now - first.at <= staleMs) return first;
    }
    return std::nullopt;
}

bool BodyState::has(BodyKey key) const { return contains(buttons, uint16_t(key)); }
bool BodyState::has(BodyDial dial) const { return contains(dials, uint16_t(dial)); }
bool BodyState::operator==(const BodyState& other) const {
    return buttons == other.buttons && dials == other.dials && resultId == other.resultId && result == other.result;
}

bool WheelGesture::press(int x, int y) {
    const auto r2 = x * x + y * y;
    active_ = r2 <= outerRadius * outerRadius;
    if (!active_) return false;
    startX_ = x; startY_ = y;
    ring_ = r2 >= centerRadius * centerRadius;
    last_ = degrees(x, y);
    accumulated_ = turned_ = 0;
    moved_ = false;
    return true;
}
int WheelGesture::move(int x, int y) {
    if (!active_) return 0;
    const int dx = x - startX_, dy = y - startY_;
    if (dx * dx + dy * dy > 24 * 24) moved_ = true;
    // Angles near the centre are dominated by finger jitter.
    if (!ring_ || x * x + y * y < 24 * 24) return 0;
    const float angle = degrees(x, y);
    float delta = angle - last_;
    if (delta > 180) delta -= 360;
    if (delta <= -180) delta += 360;
    last_ = angle;
    accumulated_ += delta;
    turned_ += std::fabs(delta);
    if (turned_ >= 8) moved_ = true;
    int steps = 0;
    while (std::fabs(accumulated_) >= stepDegrees) {
        const int direction = accumulated_ > 0 ? 1 : -1;
        accumulated_ -= float(direction * stepDegrees);
        steps += direction;
    }
    return steps;
}
WheelTap WheelGesture::release(int x, int y) {
    if (!active_) return WheelTap::None;
    const int dx = x - startX_, dy = y - startY_;
    if (dx * dx + dy * dy > 24 * 24) moved_ = true;
    active_ = false;
    if (moved_) return WheelTap::None;
    if (!ring_) return WheelTap::Set;
    const float a = degrees(startX_, startY_);
    if (a >= -135 && a < -45) return WheelTap::Up;
    if (a >= -45 && a < 45) return WheelTap::Right;
    if (a >= 45 && a < 135) return WheelTap::Down;
    return WheelTap::Left;
}
}
