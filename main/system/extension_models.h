#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace toolbox {
enum class Extension { Counter, Timers, Focus, Level, Weather };
enum class AppKey { None, A, B, HoldA, HoldB, Home };

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

struct LevelReading {
    double x = 0, y = 0, z = 1;
    double filtered_x = 0, filtered_y = 0, filtered_z = 1;
    double tilt = 0, pitch = 0, roll = 0, direction = 0, error = 0;
    bool valid = false;
    void sample(double ax, double ay, double az, const double *bias, const double *target, bool first) {
        ax -= bias[0];
        ay -= bias[1];
        az -= bias[2];
        const double magnitude = std::sqrt(ax * ax + ay * ay + az * az);
        if (!std::isfinite(magnitude) || magnitude < .5 || magnitude > 1.5) {
            valid = false;
            return;
        }
        const double alpha = first ? 1 : .08;
        filtered_x += alpha * (ax / magnitude - filtered_x);
        filtered_y += alpha * (ay / magnitude - filtered_y);
        filtered_z += alpha * (az / magnitude - filtered_z);
        const double length = std::sqrt(filtered_x * filtered_x + filtered_y * filtered_y + filtered_z * filtered_z);
        if (length < .000001) {
            valid = false;
            return;
        }
        x = filtered_x / length;
        y = filtered_y / length;
        z = filtered_z / length;
        constexpr double degrees = 57.295779513;
        tilt = std::atan2(std::hypot(x, y), z) * degrees;
        pitch = std::atan2(x, std::hypot(y, z)) * degrees;
        roll = std::atan2(y, std::hypot(x, z)) * degrees;
        error = std::acos(std::clamp(x * target[0] + y * target[1] + z * target[2], -1., 1.)) * degrees;
        const double dx = target[0] - x, dy = target[1] - y;
        if (std::hypot(dx, dy) > .0008)
            direction = std::atan2(dy, dx) * degrees;
        valid = true;
    }
};
} // namespace toolbox
