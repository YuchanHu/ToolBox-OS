#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace toolbox::camera::sony {
// Sony RMT-P1BT-compatible commands. See coral/freemote protocol notes.
constexpr std::array<uint8_t, 16> service_uuid = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0x80};
constexpr uint16_t command_uuid = 0xff01, notify_uuid = 0xff02;
constexpr std::array<uint8_t, 4> photo_sequence = {0x07, 0x09, 0x08, 0x06};
constexpr std::array<uint8_t, 2> movie_sequence = {0x0f, 0x0e};
inline bool advertisement(const uint8_t* data, size_t size)
{
    return size >= 4 && data[0] == 0x2d && data[1] == 0x01 && data[2] == 0x03 && data[3] == 0x00;
}
inline bool recording_notification(const uint8_t* data, size_t size, bool& recording)
{
    if (size != 3 || data[0] != 2 || data[1] != 0xd5 || (data[2] != 0 && data[2] != 0x20)) return false;
    recording = data[2] == 0x20;
    return true;
}
}
