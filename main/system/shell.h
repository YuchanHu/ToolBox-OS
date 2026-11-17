#pragma once
#include <lvgl.h>
#include <functional>

namespace toolbox {
// Called with the LVGL mutex held. The shell's overlays outlive foreground Apps.
void shell_init();
void shell_attach(lv_obj_t* drawer);
void shell_detach();
bool shell_touch(bool pressed, int x, int y, uint32_t sample_tick = UINT32_MAX, bool draw = true);
void shell_cancel_touch();
bool shell_is_locked();
void shell_lock();
void shell_resume_drawer();
void shell_note_activity();
uint32_t shell_idle_ms();
int shell_home_timeout();
int shell_power_timeout();
void shell_open_settings(std::function<void()> on_close = {});
void shell_close_settings();
}
