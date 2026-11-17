/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <system/stopwatch_ui.h>
#include "model/stopwatch.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>
#include <atomic>

/**
 * @brief Derived App
 *
 */
class AppStopWatch : public mooncake::AppAbility {
public:
    AppStopWatch();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<toolbox::StopwatchPage> _view;
    model::Stopwatch stopwatch_;
    std::atomic<bool> done_{false};
    std::unique_ptr<input::KeyManager> _key_manager;
    void left_action();
    void toggle_action();
};
