/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <vector>
#include <cstdint>

namespace model {

class Stopwatch {
public:
    enum class State_t { Stopped, Running, Paused };

    void start();
    void pause();
    void reset();
    void lap();

    uint32_t getElapsedTime() const;
    const std::vector<uint32_t>& getLaps() const;
    State_t getState() const;

private:
    uint32_t _start_time   = 0;
    uint32_t _elapsed_time = 0;
    State_t _state         = State_t::Stopped;
    std::vector<uint32_t> _laps;

    uint32_t millis() const;
};

}  // namespace model
