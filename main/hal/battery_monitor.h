#pragma once

#include <algorithm>
#include <cstdint>

namespace toolbox {

struct BatteryStatus {
    uint8_t percent = 0;
    bool external_power = false;
    bool charging = false;
};

class BatteryMonitor {
public:
    BatteryStatus sample(uint16_t battery_mv, uint16_t input_mv, bool charge_active)
    {
        // Keep fractional millivolts so a steady 4200 mV can settle at 4200.
        const uint32_t voltage = battery_mv * voltage_scale;
        filtered_voltage_ = filtered_voltage_ == 0 ? voltage : (7 * filtered_voltage_ + voltage + 4) / 8;
        const int filtered_mv = (filtered_voltage_ + voltage_scale / 2) / voltage_scale;
        const bool external_power = input_mv > 4000;
        const bool charging = external_power && charge_active;

        const bool input_good = input_mv >= 4500;
        if (!input_good || filtered_mv < full_release_mv) {
            near_full_charging_samples_ = 0;
        } else if (charging) {
            near_full_charging_samples_ = battery_mv >= near_float_mv
                ? std::min(near_full_charging_samples_ + 1, near_float_samples)
                : 0;
        }

        // DONE is not wired to the PMIC. Remember a near-float charge phase so
        // the drop in terminal voltage after CHRG releases can still qualify.
        // On boot we have no previous charge samples, so require a higher
        // resting voltage. Do not discard a confirmed full reading on top-up.
        if (filtered_mv < full_release_mv) full_samples_ = 0;
        if (full_samples_ < full_confirmation_samples) {
            const int minimum_idle_mv = near_full_charging_samples_ == near_float_samples
                ? full_release_mv
                : full_at_boot_mv;
            const bool near_full_and_idle = input_good && !charge_active &&
                battery_mv >= minimum_idle_mv && filtered_mv >= minimum_idle_mv;
            full_samples_ = near_full_and_idle ? full_samples_ + 1 : 0;
        }

        const int max_percent = external_power ? 99 : 100;
        const int estimate = std::clamp((filtered_mv - 3300) * 100 / (4200 - 3300), 0, max_percent);
        const int percent = full_samples_ == full_confirmation_samples ? 100 : estimate;
        return {static_cast<uint8_t>(percent), external_power, charging};
    }

    void missed_sample()
    {
        near_full_charging_samples_ = 0;
        if (full_samples_ < full_confirmation_samples) full_samples_ = 0;
    }

private:
    static constexpr uint32_t voltage_scale = 256;
    static constexpr int near_float_mv = 4160;
    static constexpr int full_at_boot_mv = 4130;
    static constexpr int full_release_mv = 4100;
    // The PMIC sampler runs once per second.
    static constexpr int near_float_samples = 3;
    static constexpr int full_confirmation_samples = 10;

    uint32_t filtered_voltage_ = 0;
    int near_full_charging_samples_ = 0;
    int full_samples_ = 0;
};

}  // namespace toolbox
