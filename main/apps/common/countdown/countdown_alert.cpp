#include "countdown_alert.h"
#include "countdown.h"
#include <apps/common/audio/audio.h>
#include <hal/hal.h>
#include <cstdio>

namespace toolbox {
void CountdownAlert::start(int64_t now)
{
    until_ = now + 3000000;
    next_ = now;
}

void CountdownAlert::update(int64_t now)
{
    if (until_ <= now || now < next_) return;
    audio::play_tone(1760, .07f, .4f);
    GetHAL().vibrate(70, 70);
    next_ = now + 600000;
}

void CountdownAlert::reset()
{
    if (!until_) return;
    until_ = next_ = 0;
    audio::stop();
    GetHAL().stopVibrate();
}

std::string countdown_text(const Countdown& clock)
{
    const int64_t seconds = (clock.remaining + 999999) / 1000000;
    char text[32];
    if (seconds >= 3600)
        snprintf(text, sizeof(text), "%02lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        snprintf(text, sizeof(text), "%02lld:%02lld", seconds / 60, seconds % 60);
    return text;
}
} // namespace toolbox
