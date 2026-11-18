#pragma once
#include <cstdint>
#include <string>

namespace toolbox {
struct Countdown;
class CountdownAlert {
  public:
    void start(int64_t now);
    void update(int64_t now);
    void reset();
  private:
    int64_t until_ = 0, next_ = 0;
};
std::string countdown_text(const Countdown& clock);
} // namespace toolbox
