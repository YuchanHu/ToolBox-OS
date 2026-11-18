#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace toolbox {
enum class TouchEvent { Down, Move, Up, Cancel };

struct TouchSample {
    bool pressed = false;
    int16_t x = -1, y = -1;
    uint32_t tick = 0;
    bool cancel = false;
    uint32_t sampled_us = 0;
    TouchEvent event = TouchEvent::Up;
};

// The HAL protects this history with a short critical section. Sampling never
// waits for LVGL. Overflow cancels the gesture and suppresses it until release.
class TouchHistory {
public:
    void push(TouchSample sample)
    {
        const auto previous = latest;
        latest = sample;
        if (sample.cancel) {
            cancel(sample.tick);
            discard_until_release_ = sample.pressed || previous.pressed;
            return;
        }
        if (discard_until_release_) {
            if (!sample.pressed) discard_until_release_ = false;
            return;
        }
        if (sample.pressed == previous.pressed &&
            (!sample.pressed || (sample.x == previous.x && sample.y == previous.y &&
                                sample.tick - last_queued_tick_ < 16))) return;
        sample.event = !sample.pressed ? TouchEvent::Up
            : previous.pressed ? TouchEvent::Move : TouchEvent::Down;
        // Preserve the first movement and the latest pair for flicks and reversals.
        if (sample.event == TouchEvent::Move && count_ >= 3 &&
            at(count_ - 3).event == TouchEvent::Move &&
            at(count_ - 2).event == TouchEvent::Move &&
            at(count_ - 1).event == TouchEvent::Move) {
            at(count_ - 2) = at(count_ - 1);
            at(count_ - 1) = sample;
            last_queued_tick_ = sample.tick;
            return;
        }
        if (count_ == samples_.size()) {
            ++overflows_;
            cancel(sample.tick);
            discard_until_release_ = sample.pressed;
            return;
        }
        at(count_) = sample;
        last_queued_tick_ = sample.tick;
        ++count_;
    }
    void cancel(uint32_t tick)
    {
        head_ = 0;
        count_ = 1;
        samples_[0] = {false, latest.x, latest.y, tick, true, 0, TouchEvent::Cancel};
        discard_until_release_ = true;
    }
    bool pop(TouchSample& sample)
    {
        if (!count_) return false;
        sample = samples_[head_];
        head_ = (head_ + 1) % samples_.size();
        --count_;
        return true;
    }
    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    uint32_t overflows() const { return overflows_; }
    TouchSample latest;
private:
    std::array<TouchSample, 64> samples_{};
    size_t head_ = 0, count_ = 0;
    bool discard_until_release_ = false;
    uint32_t last_queued_tick_ = 0;
    uint32_t overflows_ = 0;
    TouchSample& at(size_t index) { return samples_[(head_ + index) % samples_.size()]; }
};
}
