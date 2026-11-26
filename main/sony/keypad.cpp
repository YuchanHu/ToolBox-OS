#include "keypad.h"
#include <array>

namespace sony {
namespace {
constexpr std::array<std::string_view, 9> lower{"abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"," "};
constexpr std::array<std::string_view, 9> upper{"ABC","DEF","GHI","JKL","MNO","PQRS","TUV","WXYZ"," "};
constexpr std::string_view numbers = "123456789";
// Printable punctuation followed by space exactly once across the symbol pages.
constexpr std::string_view punctuation = "!%.=_-+,@#$^&*()[]{}:;?/\\|~`<>\"' ";
constexpr unsigned pages = (punctuation.size() + 8) / 9;
}
void Keypad::reset(std::string_view seed, size_t limit) {
    volatile char* bytes = value_.data();
    for (size_t i = 0; i < value_.size(); ++i) bytes[i] = 0;
    value_.assign(seed);
    limit_ = limit;
    mode_ = Mode::Lower;
    symbols_ = 0;
    pendingKey_ = -1;
}
void Keypad::setMode(Mode mode) { commit(); mode_ = mode; }
void Keypad::append(char c) { if (value_.size() < limit_) value_ += c; }
std::string_view Keypad::key(unsigned index) const {
    if (index >= 9) return {};
    switch (mode_) {
    case Mode::Lower: return lower[index];
    case Mode::Upper: return upper[index];
    case Mode::Number: return numbers.substr(index, 1);
    case Mode::Symbol:
        index += symbols_ * 9;
        return index < punctuation.size() ? punctuation.substr(index, 1) : std::string_view{};
    }
    return {};
}
void Keypad::commit() {
    if (pendingKey_ < 0) return;
    append(key(pendingKey_)[choice_]);
    pendingKey_ = -1;
}
bool Keypad::tick(uint32_t now) {
    if (pendingKey_ < 0 || now - lastTap_ < 850) return false;
    commit();
    return true;
}
void Keypad::tap(unsigned index, uint32_t now) {
    tick(now);
    auto letters = key(index);
    if (letters.empty()) return;
    if (letters.size() == 1) { commit(); append(letters.front()); return; }
    if (pendingKey_ == int(index)) choice_ = (choice_ + 1) % letters.size();
    else {
        commit();
        if (value_.size() == limit_) return;
        pendingKey_ = index;
        choice_ = 0;
    }
    lastTap_ = now;
}
void Keypad::extra() {
    commit();
    if (mode_ == Mode::Number) append('0');
    else if (mode_ == Mode::Symbol) symbols_ = (symbols_ + 1) % pages;
}
void Keypad::erase() {
    if (pendingKey_ >= 0) pendingKey_ = -1;
    else if (!value_.empty()) { value_.back() = 0; value_.pop_back(); }
}
std::string Keypad::display() const {
    return pendingKey_ < 0 ? value_ : value_ + key(pendingKey_)[choice_];
}
}
