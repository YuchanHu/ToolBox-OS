/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "stopwatch.h"
#include <hal/hal.h>

using namespace model;

void Stopwatch::start()
{
    if (_state == State_t::Stopped || _state == State_t::Paused) {
        _start_time = millis();
        _state      = State_t::Running;
    }
}

void Stopwatch::pause()
{
    if (_state == State_t::Running) {
        _elapsed_time += millis() - _start_time;
        _state = State_t::Paused;
    }
}

void Stopwatch::reset()
{
    _start_time   = 0;
    _elapsed_time = 0;
    _laps.clear();
    _state = State_t::Stopped;
}

void Stopwatch::lap()
{
    if (_state == State_t::Running) {
        uint32_t currentTime = millis();
        uint32_t lapTime     = _elapsed_time + (currentTime - _start_time);
        _laps.push_back(lapTime);
    }
}

uint32_t Stopwatch::getElapsedTime() const
{
    if (_state == State_t::Running) {
        return _elapsed_time + (millis() - _start_time);
    }
    return _elapsed_time;
}

const std::vector<uint32_t>& Stopwatch::getLaps() const
{
    return _laps;
}

Stopwatch::State_t Stopwatch::getState() const
{
    return _state;
}

uint32_t Stopwatch::millis() const
{
    return GetHAL().millis();
}
