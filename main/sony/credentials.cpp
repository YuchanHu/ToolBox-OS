#include "connection.h"
#include <algorithm>
#include <array>
#include <utility>

namespace sony {
namespace {
constexpr std::array<size_t, 4> limits{32, 64, 128, 256};
int hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
bool decode(std::string_view input, std::string& output, size_t maximum) {
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char ch = input[i];
        if (ch == '+') ch = ' ';
        else if (ch == '%') {
            if (input.size() - i < 3) return false;
            int hi = hex(input[i + 1]), lo = hex(input[i + 2]);
            if (hi < 0 || lo < 0) return false;
            ch = hi * 16 + lo;
            i += 2;
        }
        if (!ch || output.size() == maximum) return false;
        output += char(ch);
    }
    return true;
}
void wipe(std::string& value) {
    volatile char* bytes = value.data();
    for (size_t i = 0; i < value.size(); ++i) bytes[i] = 0;
    value.clear();
}
}
bool validCredential(unsigned field, std::string_view value) {
    if (field >= limits.size() || value.empty() || value.size() > limits[field]) return false;
    if (field == 1 && value.size() < 8) return false;
    return std::all_of(value.begin(), value.end(), [field, size = value.size()] (unsigned char ch) {
        if (field == 0) return ch >= 32 && ch != 127;
        if (field == 1 && size == 64) return hex(ch) >= 0;
        return ch >= 32 && ch <= 126;
    });
}
bool CameraCredentials::valid() const {
    return validCredential(0, ssid) && validCredential(1, wifiPassword) &&
           validCredential(2, user) && validCredential(3, sshPassword);
}
void CameraCredentials::clear() {
    ssid.clear();
    wipe(wifiPassword);
    wipe(user);
    wipe(sshPassword);
    fingerprint.clear();
}
CameraCredentials::~CameraCredentials() { clear(); }
bool readProvisionForm(std::string_view form, CameraCredentials& out) {
    CameraCredentials parsed;
    const std::array<std::string_view, 4> names{"ssid", "wifi", "user", "password"};
    std::array<std::string*, 4> fields{&parsed.ssid, &parsed.wifiPassword, &parsed.user, &parsed.sshPassword};
    unsigned seen = 0;
    while (!form.empty()) {
        auto end = form.find('&');
        auto part = form.substr(0, end);
        auto split = part.find('=');
        if (split == part.npos) return false;
        auto it = std::find(names.begin(), names.end(), part.substr(0, split));
        if (it == names.end()) return false;
        size_t index = it - names.begin();
        unsigned bit = 1u << index;
        if ((seen & bit) || !decode(part.substr(split + 1), *fields[index], limits[index])) return false;
        seen |= bit;
        if (end == form.npos) break;
        form.remove_prefix(end + 1);
    }
    if (!parsed.valid()) return false;
    out.clear();
    out = parsed;
    return true;
}
}
