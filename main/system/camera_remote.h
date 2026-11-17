#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace toolbox::camera {
enum class Phase { Off, Starting, Scanning, Connecting, Pairing, Ready, Error };
enum class Mode { Photo, Movie };
struct Device {
    std::array<uint8_t, 6> address{};
    uint8_t address_type = 0;
    std::string name;
    int rssi = 0;
};
struct Status {
    Phase phase = Phase::Off;
    std::string message;
    std::string name;
    std::vector<Device> devices;
    bool busy = false;
    bool recording_known = false;
    bool recording = false;
    uint32_t recording_since = 0;
};
void start();
void stop();
void tick();
void connect(const Device& device);
void trigger(Mode mode);
Status status();
}
