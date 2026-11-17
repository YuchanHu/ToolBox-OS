/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include <atomic>

/**
 * @brief Derived App
 *
 */
class AppSetup : public mooncake::AppAbility {
public:
    AppSetup();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::atomic<bool> done_{false};
    std::unique_ptr<input::KeyManager> _key_manager;
};
