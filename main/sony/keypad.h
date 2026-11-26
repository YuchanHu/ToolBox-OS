#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace sony {
class Keypad {
public:
    enum class Mode { Lower, Upper, Number, Symbol };
    ~Keypad() { reset({}, 0); }
    void reset(std::string_view seed, size_t limit);
    void setMode(Mode mode);
    Mode mode() const { return mode_; }
    std::string_view key(unsigned index) const;
    void tap(unsigned index, uint32_t now);
    void extra();
    void erase();
    bool tick(uint32_t now);
    void commit();
    std::string display() const;
    const std::string& value() const { return value_; }
private:
    void append(char c);
    std::string value_;
    size_t limit_ = 0;
    Mode mode_ = Mode::Lower;
    unsigned symbols_ = 0;
    int pendingKey_ = -1;
    size_t choice_ = 0;
    uint32_t lastTap_ = 0;
};
}
