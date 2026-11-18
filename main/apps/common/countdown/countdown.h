#pragma once
#include <algorithm>
#include <cstdint>

namespace toolbox {
// Monotonic microseconds keep timing independent of render cadence and millis wrap.
struct Countdown {
    enum class State { Ready, Running, Paused, Done };
    State state = State::Ready;
    int64_t duration = 600000000, remaining = duration, deadline = 0;
    void reset(int64_t value) {
        duration = remaining = value;
        state = State::Ready;
    }
    void toggle(int64_t now) {
        if (state == State::Running) {
            remaining = std::max<int64_t>(0, deadline - now);
            state = State::Paused;
        } else {
            if (state == State::Done)
                remaining = duration;
            deadline = now + remaining;
            state = State::Running;
        }
    }
    bool tick(int64_t now) {
        if (state != State::Running)
            return false;
        remaining = std::max<int64_t>(0, deadline - now);
        if (remaining)
            return false;
        state = State::Done;
        return true;
    }
};
} // namespace toolbox
