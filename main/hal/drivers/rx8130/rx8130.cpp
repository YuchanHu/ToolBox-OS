/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rx8130.h"

// RX-8130 Register definitions
#define RX8130_REG_SEC   0x10
#define RX8130_REG_MIN   0x11
#define RX8130_REG_HOUR  0x12
#define RX8130_REG_WDAY  0x13
#define RX8130_REG_MDAY  0x14
#define RX8130_REG_MONTH 0x15
#define RX8130_REG_YEAR  0x16

#define RX8130_REG_ALMIN   0x17
#define RX8130_REG_ALHOUR  0x18
#define RX8130_REG_ALWDAY  0x19
#define RX8130_REG_TCOUNT0 0x1A
#define RX8130_REG_TCOUNT1 0x1B
#define RX8130_REG_EXT     0x1C
#define RX8130_REG_FLAG    0x1D
#define RX8130_REG_CTRL0   0x1E
#define RX8130_REG_CTRL1   0x1F

#define RX8130_REG_END 0x23

// Extension Register (1Ch) bit positions
#define RX8130_BIT_EXT_TSEL (7 << 0)
#define RX8130_BIT_EXT_WADA (1 << 3)
#define RX8130_BIT_EXT_TE   (1 << 4)
#define RX8130_BIT_EXT_USEL (1 << 5)
#define RX8130_BIT_EXT_FSEL (3 << 6)

// Flag Register (1Dh) bit positions
#define RX8130_BIT_FLAG_VLF (1 << 1)
#define RX8130_BIT_FLAG_AF  (1 << 3)
#define RX8130_BIT_FLAG_TF  (1 << 4)
#define RX8130_BIT_FLAG_UF  (1 << 5)

// Control 0 Register (1Еh) bit positions
#define RX8130_BIT_CTRL_TSTP (1 << 2)
#define RX8130_BIT_CTRL_AIE  (1 << 3)
#define RX8130_BIT_CTRL_TIE  (1 << 4)
#define RX8130_BIT_CTRL_UIE  (1 << 5)
#define RX8130_BIT_CTRL_STOP (1 << 6)
#define RX8130_BIT_CTRL_TEST (1 << 7)

static uint8_t bcd2dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0f);
}

static uint8_t dec2bcd(uint8_t val)
{
    return ((val / 10) << 4) + (val % 10);
}

// Helper function to check if a year is a leap year
static bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Helper function to get the number of days in a given month
// Note: month is 1-12 for this function
static int getDaysInMonth(int month, int year)
{
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return daysInMonth[month - 1];
}

bool RX8130_Class::begin(i2c_bus_handle_t bus_handle, uint8_t i2c_addr)
{
    _addr = i2c_addr;
    if (_i2c_dev) {
        i2c_bus_device_delete(&_i2c_dev);
    }
    _i2c_dev = i2c_bus_device_create(bus_handle, _addr, 0);
    if (_i2c_dev == NULL) {
        return false;
    }
    return true;
}

void RX8130_Class::initBat()
{
    uint8_t data;
    i2c_bus_read_byte(_i2c_dev, RX8130_REG_CTRL1, &data);
    // Enable backup battery charging
    data |= 0b00110000;
    i2c_bus_write_byte(_i2c_dev, RX8130_REG_CTRL1, data);
}

bool RX8130_Class::setTime(const struct tm *time)
{
    if (!_i2c_dev || time->tm_year < 100 || time->tm_year > 199 ||
        time->tm_mon < 0 || time->tm_mon > 11 || time->tm_mday < 1 ||
        time->tm_mday > getDaysInMonth(time->tm_mon + 1, time->tm_year + 1900) ||
        time->tm_hour < 0 || time->tm_hour > 23 || time->tm_min < 0 || time->tm_min > 59 ||
        time->tm_sec < 0 || time->tm_sec > 59 || time->tm_wday < 0 || time->tm_wday > 6) return false;

    uint8_t control = 0;
    if (i2c_bus_read_byte(_i2c_dev, RX8130_REG_CTRL0, &control) != ESP_OK) return false;
    if (i2c_bus_write_byte(_i2c_dev, RX8130_REG_CTRL0, control | RX8130_BIT_CTRL_STOP) != ESP_OK)
        return false;
    const uint8_t date[7] = {dec2bcd(time->tm_sec), dec2bcd(time->tm_min), dec2bcd(time->tm_hour),
        static_cast<uint8_t>(1U << time->tm_wday), dec2bcd(time->tm_mday),
        dec2bcd(time->tm_mon + 1), dec2bcd(time->tm_year - 100)};
    const bool written = i2c_bus_write_bytes(_i2c_dev, RX8130_REG_SEC, 7, date) == ESP_OK;
    // Always try to restart the oscillator, including after a failed write.
    const bool resumed = i2c_bus_write_byte(_i2c_dev, RX8130_REG_CTRL0,
                                           control & ~RX8130_BIT_CTRL_STOP) == ESP_OK;
    if (!written || !resumed) return false;
    uint8_t flags = 0;
    if (i2c_bus_read_byte(_i2c_dev, RX8130_REG_FLAG, &flags) != ESP_OK) return false;
    return i2c_bus_write_byte(_i2c_dev, RX8130_REG_FLAG, flags & ~RX8130_BIT_FLAG_VLF) == ESP_OK;
}

bool RX8130_Class::getTime(struct tm *time)
{
    uint8_t flags = 0, date[7] = {};
    if (!_i2c_dev || i2c_bus_read_byte(_i2c_dev, RX8130_REG_FLAG, &flags) != ESP_OK ||
        (flags & RX8130_BIT_FLAG_VLF) ||
        i2c_bus_read_bytes(_i2c_dev, RX8130_REG_SEC, 7, date) != ESP_OK) return false;
    struct tm result = {};
    result.tm_sec = bcd2dec(date[0] & 0x7f);
    result.tm_min = bcd2dec(date[1] & 0x7f);
    result.tm_hour = bcd2dec(date[2] & 0x3f);
    result.tm_mday = bcd2dec(date[4] & 0x3f);
    result.tm_mon = bcd2dec(date[5] & 0x1f) - 1;
    result.tm_year = bcd2dec(date[6]) + 100;
    if (result.tm_mon < 0 || result.tm_mon > 11 || result.tm_mday < 1 ||
        result.tm_mday > getDaysInMonth(result.tm_mon + 1, result.tm_year + 1900) ||
        result.tm_year > 199 || result.tm_hour > 23 || result.tm_min > 59 || result.tm_sec > 59) return false;
    int days = 0;
    for (int year = 2000; year < result.tm_year + 1900; ++year)
        days += isLeapYear(year) ? 366 : 365;
    for (int month = 1; month <= result.tm_mon; ++month)
        days += getDaysInMonth(month, result.tm_year + 1900);
    // Derive weekday from the calendar for older firmware's numeric encoding.
    result.tm_wday = (6 + days + result.tm_mday - 1) % 7; // 2000-01-01 Saturday
    result.tm_isdst = 0;
    *time = result;
    return true;
}

void RX8130_Class::clearIrqFlags()
{
    i2c_bus_write_byte(_i2c_dev, RX8130_REG_FLAG, 0);
}

void RX8130_Class::disableIrq()
{
    i2c_bus_write_byte(_i2c_dev, RX8130_REG_CTRL0, 0);
}
