/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <mooncake_log.h>
#include <M5PM1.h>
#include <memory>
#include <mutex>

static const std::string_view _tag = "HAL-PMIC";
static std::unique_ptr<M5PM1> _pm1;

void Hal::powerOff()
{
    if (_pm1) _pm1->shutdown();
}

namespace {

constexpr auto charge_status_pin = M5PM1_GPIO_NUM_2;
constexpr auto charge_program_pin = M5PM1_GPIO_NUM_3;
constexpr uint32_t battery_sample_period_ms = 1000;
toolbox::BatteryMonitor battery_monitor;
toolbox::BatteryStatus battery_status;
std::mutex battery_status_mutex;

void sample_battery()
{
    uint16_t battery_mv = 0;
    uint16_t input_mv = 0;
    uint8_t charge_status = 1;
    if (_pm1->readVbat(&battery_mv) != M5PM1_OK ||
        _pm1->readVin(&input_mv) != M5PM1_OK ||
        _pm1->gpioGetInput(charge_status_pin, &charge_status) != M5PM1_OK) {
        battery_monitor.missed_sample();
        return;
    }

    const auto next = battery_monitor.sample(battery_mv, input_mv, charge_status == 0);
    toolbox::BatteryStatus previous;
    {
        std::lock_guard<std::mutex> lock(battery_status_mutex);
        previous = battery_status;
        battery_status = next;
    }
    static bool first_sample = true;
    if (first_sample || previous.external_power != next.external_power || previous.charging != next.charging ||
        (previous.percent == 100) != (next.percent == 100)) {
        mclog::tagInfo(_tag, "battery: vbat={} mV vin={} mV chg_stat={} level={}%",
                       battery_mv, input_mv, charge_status, next.percent);
        first_sample = false;
    }
}

void bat_reading_task(void*)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(battery_sample_period_ms));
        sample_battery();
    }
}

}  // namespace

void Hal::pmic_init()
{
    mclog::tagInfo(_tag, "pmic init");

    _pm1 = std::make_unique<M5PM1>();
    if (_pm1->begin(i2c_bus_get_internal_bus_handle(_i2c_bus)) != M5PM1_OK) {
        mclog::tagInfo(_tag, "init failed");
        _pm1.reset();
        return;
    }

    _pm1->setI2cSleepTime(0);

    // set button delay click 1s
    _pm1->btnSetConfig(M5PM1_BTN_TYPE_CLICK, M5PM1_BTN_CLICK_DELAY_1000MS);
    // disable WDT, default is open
    _pm1->wdtSet(0);
    //  hold LDO power close when power off, keep power for RTC
    _pm1->ldoSetPowerHold(true);

    // Enable charging through shutdown and use the existing 425 mA profile.
    const auto charge_enable = _pm1->setChargeEnable(true);
    const auto charge_program = _pm1->gpioSet(charge_program_pin, M5PM1_GPIO_MODE_OUTPUT, 0,
                                             M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
    const auto status_func = _pm1->gpioSetFunc(charge_status_pin, M5PM1_GPIO_FUNC_GPIO);
    const auto status_mode = _pm1->gpioSetMode(charge_status_pin, M5PM1_GPIO_MODE_INPUT);
    const auto status_pull = _pm1->gpioSetPull(charge_status_pin, M5PM1_GPIO_PULL_NONE);
    if (charge_enable != M5PM1_OK || charge_program != M5PM1_OK || status_func != M5PM1_OK ||
        status_mode != M5PM1_OK || status_pull != M5PM1_OK) {
        mclog::tagWarn(_tag, "charger init: enable={} program={} status func={} mode={} pull={}",
                       static_cast<int>(charge_enable), static_cast<int>(charge_program), static_cast<int>(status_func),
                       static_cast<int>(status_mode), static_cast<int>(status_pull));
    }
    _pm1->setSingleResetDisable(true);

    sample_battery();
    if (xTaskCreate(bat_reading_task, "bat_reading", 4 * 1024, nullptr, 1, nullptr) != pdPASS) {
        mclog::tagWarn(_tag, "could not start battery sampler");
    }
}

bool Hal::pmic_get_pwr_btn_state()
{
    bool result = false;
    if (_pm1) {
        return _pm1->btnGetState(&result);
    }
    return result;
}

void Hal::setPowerLed(bool enabled)
{
    if (_pm1) {
        _pm1->setLedEnLevel(enabled);
    }
}

toolbox::BatteryStatus Hal::getBatteryStatus()
{
    std::lock_guard<std::mutex> lock(battery_status_mutex);
    return battery_status;
}

uint8_t Hal::getBatteryLevel()
{
    return getBatteryStatus().percent;
}

bool Hal::isBatteryCharging(bool strict)
{
    const auto status = getBatteryStatus();
    return strict ? status.charging : status.external_power;
}
