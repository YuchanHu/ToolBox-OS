#include "shell.h"
#include "fonts.h"
#include "ui.h"
#include "ble.h"
#include "runtime.h"
#include <src/core/lv_obj_draw_private.h>
#include <src/misc/lv_area_private.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <hal/hal.h>
#include <hal/refresh_metrics.h>
#include <hal/utils/settings/settings.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <atomic>

namespace toolbox {
namespace {
constexpr uint32_t accent = 0x9dc6ff;
constexpr int wipe_height = ui::screen_diameter;
lv_obj_t *drawer, *lockscreen, *lock_content, *digital, *date_label, *status, *dial;
lv_obj_t *overlay, *clock_panel, *clock_time, *clock_battery, *controls;
lv_obj_t *control_battery, *wifi_button, *settings, *dial_date, *dial_battery;
lv_obj_t *brightness, *volume, *ble_button, *network_text;
lv_obj_t *network_name = nullptr;
lv_obj_t *control_time, *brightness_value, *volume_value, *dial_battery_icon;
lv_obj_t *lock_wifi, *lock_battery, *dial_day, *lock_status_row;
lv_obj_t *clock_battery_icon, *control_battery_icon;
lv_obj_t *unlock_wipe = nullptr;
lv_draw_buf_t *unlock_lock_pixels = nullptr, *unlock_drawer_pixels = nullptr;
// Cached pixels of the settled control center, used to slide the sheet while
// closing it instead of re-rendering every widget for each drag step.
lv_obj_t *controls_motion = nullptr;
lv_draw_buf_t *controls_pixels = nullptr;
bool controls_captured = false;
int controls_sheet_y = 0;
bool overlay_face_cached = false, backdrop_dimmed = false;

// Exponentially-smoothed vertical drag speed, shared by the unlock-swipe and
// pull-down gestures so both can do a velocity-aware release animation.
struct VelocityTracker {
    bool sampled = false;
    int y = 0;
    uint32_t tick = 0;
    float velocity = 0;
    void reset(int start_y, uint32_t now) { sampled = false; y = start_y; tick = now; velocity = 0; }
    void sample(int current_y, uint32_t now) {
        const uint32_t elapsed = now - tick;
        if (!elapsed || y == current_y) return;
        const float speed = static_cast<float>(y - current_y) / elapsed;
        velocity = sampled ? 0.65f * speed + 0.35f * velocity : speed;
        sampled = true;
        tick = now;
        y = current_y;
    }
};
VelocityTracker unlock_motion, pull_motion;
lv_obj_t *wifi_switch = nullptr;
enum class Page { None, Settings, Wifi, Ap, Faces, Ble, Display, Sound, Info, Power, Phone };
Page page = Page::None;
std::function<void()> settings_closed;
uint32_t clock_shown_at = 0;
bool overlay_closing = false;
std::array<std::array<lv_point_precise_t, 2>, 60> tick_points;
void render_page(Page target);
void update_time();
lv_obj_t *ble_status = nullptr;
bool pending_ble = false, desired_ble = false;
bool pending_provision = false, desired_provision = false;
bool provisioning_visible = false;
std::array<lv_obj_t*, 3> hands;
std::array<std::array<lv_point_precise_t, 2>, 3> hand_points;
bool locked = true, analog = false, settling = false;
int overlay_level = 0;
int dial_battery_percent = -1;
int unlock_progress = 0;
int rendered_progress = -1;
bool lock_snapshot_dirty = true;
bool drawer_snapshot_dirty = false;
std::array<int, 3> hand_steps{-1, -1, -1};
uint32_t last_second = UINT32_MAX;
bool was_pressed = false, captured = false, unlock_dragging = false;
// Live-tracked drag state for the two pull-down overlays (time card / control center).
bool pull_dragging = false, pull_closing = false;
lv_obj_t *pull_surface = nullptr;
int pull_height = 0;
int start_x, start_y, last_x, last_y;
bool pending_brightness = false, pending_volume = false, pending_wifi = false;
bool save_brightness = false, save_volume = false;
int desired_brightness = 0, desired_volume = 0;
bool desired_wifi = false;
bool pending_face = false;
std::atomic<uint32_t> activity_tick{0};
std::atomic<int> home_seconds{60}, power_seconds{600};
bool resume_drawer = false;
uint32_t shift_tick = 0;

using ui::panel;
using ui::text;
using ui::icon;
using ui::button;
using ui::icon_button;

void set_analog(bool enabled)
{
    analog = enabled;
    if (!lockscreen) return;
    lv_obj_set_flag(dial, LV_OBJ_FLAG_HIDDEN, !analog);
    lv_obj_set_flag(digital, LV_OBJ_FLAG_HIDDEN, analog);
    lv_obj_set_flag(date_label, LV_OBJ_FLAG_HIDDEN, analog);
    lv_obj_set_flag(lock_status_row, LV_OBJ_FLAG_HIDDEN, analog);
    last_second = UINT32_MAX;
    lock_snapshot_dirty = true;
}

constexpr int clock_height = 132, controls_height = 466;
constexpr lv_opa_t backdrop_opacity = 85;

int surface_height(const lv_obj_t* surface) { return surface == controls ? controls_height : clock_height; }
bool controls_sliding() { return controls_motion && !lv_obj_has_flag(controls_motion, LV_OBJ_FLAG_HIDDEN); }
int surface_y(const lv_obj_t* surface)
{
    return surface == controls && controls_sliding() ? controls_sheet_y : lv_obj_get_style_y(surface, LV_PART_MAIN);
}

// Applies a change whose pixels the caller repaints itself. Layout work that was
// already pending is flushed first so that it still invalidates normally.
template<typename Change>
void without_invalidation(Change change)
{
    auto* display = lv_obj_get_display(overlay);
    lv_obj_update_layout(overlay);
    lv_display_enable_invalidation(display, false);
    change();
    lv_obj_update_layout(overlay);
    lv_display_enable_invalidation(display, true);
}

// Bounds of what a child of the control center paints. A transparent row only
// paints its own children.
bool painted_bounds(lv_obj_t* object, lv_area_t& bounds)
{
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return false;
    auto extended = [](lv_obj_t* target) {
        lv_area_t area;
        lv_obj_get_coords(target, &area);
        const auto extra = lv_obj_get_ext_draw_size(target);
        return lv_area_t{area.x1 - extra, area.y1 - extra, area.x2 + extra, area.y2 + extra};
    };
    const uint32_t children = lv_obj_get_child_count(object);
    const bool transparent_row = children && lv_obj_check_type(object, &lv_obj_class) &&
        lv_obj_get_style_bg_opa(object, LV_PART_MAIN) == LV_OPA_TRANSP &&
        lv_obj_get_style_border_width(object, LV_PART_MAIN) == 0;
    if (!transparent_row) {
        bounds = extended(object);
        return true;
    }
    bool found = false;
    for (uint32_t index = 0; index < children; ++index) {
        auto* child = lv_obj_get_child(object, index);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        const auto area = extended(child);
        bounds = found ? lv_area_t{std::min(bounds.x1, area.x1), std::min(bounds.y1, area.y1),
                                   std::max(bounds.x2, area.x2), std::max(bounds.y2, area.y2)} : area;
        found = true;
    }
    return found;
}

// The sheet is black. Repaint the rows its edge crosses and the old and new
// positions of its visible widgets.
void invalidate_controls_move(int previous_y, int y, bool sliding)
{
    lv_area_t sheet;
    lv_obj_get_coords(controls, &sheet);
    const int shift = y - previous_y;
    const int offset = sliding ? y : 0;
    const int bottom = sheet.y2 + offset;
    const lv_area_t edge{sheet.x1, bottom - std::max(shift, 0) + 1,
                         sheet.x2, bottom - std::min(shift, 0)};
    lv_obj_invalidate_area(overlay, &edge);
    for (uint32_t index = 0; index < lv_obj_get_child_count(controls); ++index) {
        lv_area_t area;
        if (!painted_bounds(lv_obj_get_child(controls, index), area)) continue;
        area.y1 += offset - std::max(shift, 0);
        area.y2 += offset - std::min(shift, 0);
        lv_obj_invalidate_area(overlay, &area);
    }
}

// Seed the cache from the composed frame when the panel settles. After that,
// keep it synchronized using only the rows and columns each flush repaints.
void capture_control_center(lv_event_t* event)
{
    if (pull_dragging || overlay_closing || overlay_level != 2 ||
        !controls || lv_obj_has_flag(controls, LV_OBJ_FLAG_HIDDEN) || surface_y(controls) != 0 ||
        !lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN))
        return;
    auto* display = static_cast<lv_display_t*>(lv_event_get_target(event));
    if (lv_display_get_render_mode(display) == LV_DISPLAY_RENDER_MODE_PARTIAL ||
        (!controls_captured && !lv_display_flush_is_last(display))) return;
    const auto* frame = lv_display_get_buf_active(display);
    if (frame->header.cf != LV_COLOR_FORMAT_RGB565_SWAPPED) return;
    if (!controls_pixels) {
        controls_pixels = lv_draw_buf_create(lv_obj_get_width(controls), controls_height, LV_COLOR_FORMAT_RGB565_SWAPPED, 0);
        if (!controls_pixels) return;
    }
    const auto& target = controls_pixels->header;
    lv_area_t coords;
    lv_obj_get_coords(controls, &coords);
    if (coords.x1 < 0 || coords.y1 < 0 ||
        coords.x1 + target.w > frame->header.w || coords.y1 + target.h > frame->header.h) return;
    lv_area_t dirty = coords;
    if (controls_captured) {
        auto flushed = *static_cast<const lv_area_t*>(lv_event_get_param(event));
        lv_area_move(&flushed, -lv_display_get_offset_x(display), -lv_display_get_offset_y(display));
        if (!lv_area_intersect(&dirty, &flushed, &coords)) return;
    }
    const auto start = perf::now();
    lv_image_cache_drop(controls_pixels);
    for (int y = dirty.y1; y <= dirty.y2; ++y)
        std::memcpy(controls_pixels->data + (y - coords.y1) * target.stride + (dirty.x1 - coords.x1) * 2,
                    frame->data + y * frame->header.stride + dirty.x1 * 2, lv_area_get_width(&dirty) * 2);
    controls_captured = true;
    perf::sample(perf::snapshot_time, perf::now() - start);
}

// Draws the cached sheet; the panel carries the sheet's current position.
// The panel's opaque background lets LVGL skip the face beneath it.
void draw_control_sheet(lv_event_t* event)
{
    if (!controls_pixels) return;
    lv_draw_image_dsc_t image;
    lv_draw_image_dsc_init(&image);
    image.src = controls_pixels;
    lv_area_t coords;
    lv_obj_get_coords(controls_motion, &coords);
    const lv_area_t bounds{coords.x1, coords.y1, coords.x1 + controls_pixels->header.w - 1,
                           coords.y1 + controls_pixels->header.h - 1};
    lv_draw_image(lv_event_get_layer(event), &image, &bounds);
}

void start_controls_slide()
{
    if (controls_sliding() || !controls_captured) return;
    const int y = surface_y(controls);
    // The cached pixels match what is already on screen, so showing the panel
    // repaints nothing; each later step asks for its own band.
    without_invalidation([&] {
        lv_obj_add_flag(controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(controls_motion, 0, y);
        lv_obj_remove_flag(controls_motion, LV_OBJ_FLAG_HIDDEN);
    });
    controls_sheet_y = y;
}

// Ends the cached slide. `restore` brings the live widgets back at their
// settled spot; otherwise the last visible rows are invalidated so the face
// behind them reappears.
void stop_controls_slide(bool restore)
{
    if (!controls_sliding()) return;
    lv_area_t dirty;
    lv_obj_get_coords(controls_motion, &dirty);
    without_invalidation([&] {
        lv_obj_add_flag(controls_motion, LV_OBJ_FLAG_HIDDEN);
        if (restore) {
            lv_obj_set_y(controls, 0);
            lv_obj_remove_flag(controls, LV_OBJ_FLAG_HIDDEN);
        }
    });
    if (restore) lv_obj_invalidate(controls);
    else lv_obj_invalidate_area(overlay, &dirty);
}

// Moves a pull-down sheet. LVGL would repaint the whole control center for every
// step. It is opaque black, so a step changes only its content and the rows that
// its lower edge crossed.
void move_surface(lv_obj_t* surface, int y)
{
    const int previous_y = surface_y(surface);
    if (previous_y == y) return;
    if (surface == controls && controls_sliding()) {
        without_invalidation([&] { lv_obj_set_y(controls_motion, y); });
        invalidate_controls_move(previous_y, y, true);
        controls_sheet_y = y;
        return;
    }
    if (surface != controls || lv_obj_has_flag(surface, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_y(surface, y);
        lv_obj_update_layout(surface);
        return;
    }
    without_invalidation([&] { lv_obj_set_y(surface, y); });
    invalidate_controls_move(previous_y, y, false);
}

// The face stays still during a pull. Reuse the unlock snapshot beneath the
// moving sheets, including when dimming, so LVGL skips the live dial and labels.
void draw_overlay_face(lv_event_t* event)
{
    if (!overlay_face_cached) return;
    lv_draw_image_dsc_t image;
    lv_draw_image_dsc_init(&image);
    image.src = unlock_lock_pixels;
    image.opa = backdrop_dimmed ? LV_OPA_COVER - backdrop_opacity : LV_OPA_COVER;
    lv_area_t bounds;
    lv_obj_get_coords(overlay, &bounds);
    lv_draw_image(lv_event_get_layer(event), &image, &bounds);
}

void set_overlay_face(bool cached)
{
    if (overlay_face_cached == cached) return;
    overlay_face_cached = cached;
    const lv_opa_t live_opacity = backdrop_dimmed ? backdrop_opacity : LV_OPA_TRANSP;
    lv_obj_set_style_bg_opa(overlay, cached ? LV_OPA_COVER : live_opacity, 0);
}

// A settled control center covers the backdrop already.
void set_backdrop(bool dimmed)
{
    if (backdrop_dimmed == dimmed) return;
    backdrop_dimmed = dimmed;
    const auto refresh = [&] {
        if (overlay_face_cached) lv_obj_invalidate(overlay);
        else lv_obj_set_style_bg_opa(overlay, dimmed ? backdrop_opacity : LV_OPA_TRANSP, 0);
    };
    if (!lv_obj_has_flag(controls, LV_OBJ_FLAG_HIDDEN) && surface_y(controls) == 0)
        without_invalidation(refresh);
    else
        refresh();
}

void hide_overlay()
{
    // A hidden overlay ignores invalidation, so clear what it still shows first.
    set_backdrop(false);
    for (auto* surface : {clock_panel, controls}) move_surface(surface, -surface_height(surface));
    without_invalidation([] {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        set_overlay_face(false);
    });
}

// Makes `surface` the sheet being pulled. A sheet that was not showing starts
// above the screen edge, so bringing it in repaints nothing.
void present_surface(lv_obj_t* surface)
{
    lv_obj_add_flag(surface == controls ? clock_panel : controls, LV_OBJ_FLAG_HIDDEN);
    const bool sliding = surface == controls && controls_sliding();
    const bool overlay_hidden = lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    if (!sliding && (overlay_hidden || lv_obj_has_flag(surface, LV_OBJ_FLAG_HIDDEN))) {
        if (surface == controls) controls_captured = false;
        lv_obj_set_y(surface, -surface_height(surface));
        lv_obj_update_layout(surface);
        lv_obj_remove_flag(surface, LV_OBJ_FLAG_HIDDEN);
    }
    if (overlay_hidden) {
        const bool can_reuse_face = locked && lockscreen && unlock_lock_pixels && !lock_snapshot_dirty &&
            rendered_progress == 0 && page == Page::None && lv_obj_get_child(lv_screen_active(), -1) == lockscreen;
        without_invalidation([&] {
            set_overlay_face(can_reuse_face);
            lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        });
    }
    lv_obj_move_foreground(overlay);
}

void overlay_position(void* object, int32_t value)
{
    move_surface(static_cast<lv_obj_t*>(object), value);
}

// The backdrop changes once a sheet has stopped, so that its full-screen repaint
// never lands between two frames of the motion.
void overlay_settled(lv_anim_t*)
{
    overlay_closing = false;
    if (overlay_level == 0) {
        // Retire the cached slide first so its last visible rows repaint the
        // face, and fold the hand catch-up into that same repaint instead of
        // paying for a second large frame a tick later.
        stop_controls_slide(false);
        hide_overlay();
        update_time();
        return;
    }
    if (overlay_level == 1 && lv_obj_has_flag(clock_panel, LV_OBJ_FLAG_HIDDEN)) {
        // An abandoned second pull retreats and leaves the time card showing.
        lv_obj_add_flag(controls, LV_OBJ_FLAG_HIDDEN);
        move_surface(clock_panel, 0);
        lv_obj_remove_flag(clock_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (overlay_level == 2) stop_controls_slide(true);
    set_backdrop(overlay_level == 1);
}

void animate_surface(lv_obj_t* surface, int target_y, int duration)
{
    lv_anim_delete(clock_panel, overlay_position);
    lv_anim_delete(controls, overlay_position);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, surface);
    lv_anim_set_values(&animation, surface_y(surface), target_y);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_exec_cb(&animation, overlay_position);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, overlay_settled);
    lv_anim_start(&animation);
}

void show_overlay(int level)
{
    const int previous = overlay_level;
    if (level == 0) stop_controls_slide(false);
    if (level == 0 && previous == 0) {
        lv_anim_delete(clock_panel, overlay_position);
        lv_anim_delete(controls, overlay_position);
        hide_overlay();
        return;
    }
    overlay_level = level;
    last_second = UINT32_MAX;
    auto* surface = (level == 2 || previous == 2) ? controls : clock_panel;
    present_surface(surface);
    overlay_closing = level == 0;
    if (overlay_closing && overlay_face_cached) set_backdrop(false);
    if (level == 1) clock_shown_at = lv_tick_get();
    animate_surface(surface, level ? 0 : -surface_height(surface), 200);
    if (level) update_time();
    if (GetHAL().lvTouchpad) lv_indev_reset(GetHAL().lvTouchpad, nullptr);
}

void destroy_snapshot(lv_draw_buf_t*& pixels)
{
    if (!pixels) return;
    lv_image_cache_drop(pixels);
    lv_draw_buf_destroy(pixels);
    pixels = nullptr;
}

void refresh_snapshot(lv_obj_t* root, lv_draw_buf_t*& pixels)
{
    const auto start = perf::now();
    const bool hidden = lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
    auto* display = lv_obj_get_display(root);
    if (hidden) {
        lv_obj_update_layout(root);
        lv_display_enable_invalidation(display, false);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
    if (pixels) {
        lv_image_cache_drop(pixels);
        if (lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_RGB565_SWAPPED, pixels) != LV_RESULT_OK)
            destroy_snapshot(pixels);
    }
    if (!pixels) pixels = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB565_SWAPPED);
    if (hidden) {
        lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        lv_display_enable_invalidation(display, true);
    }
    perf::sample(perf::snapshot_time, perf::now() - start);
}

int wipe_edge(int revealed_pixels) { return wipe_height - revealed_pixels; }

void draw_unlock_wipe(lv_event_t* event)
{
    auto* layer = lv_event_get_layer(event);
    const lv_area_t original = layer->_clip_area;
    const int edge = wipe_edge(rendered_progress);
    lv_draw_image_dsc_t image;
    lv_draw_image_dsc_init(&image);

    auto draw_slice = [&](lv_draw_buf_t* pixels, int x, int top, int bottom) {
        top = std::max<int>(top, original.y1);
        bottom = std::min<int>(bottom, original.y2);
        if (top > bottom) return;
        layer->_clip_area = {original.x1, top, original.x2, bottom};
        lv_area_t bounds{x, 0, x + pixels->header.w - 1, pixels->header.h - 1};
        image.src = pixels;
        lv_draw_image(layer, &image, &bounds);
    };
    draw_slice(unlock_lock_pixels, lv_obj_get_x(lockscreen), 0, edge - 1);
    draw_slice(unlock_drawer_pixels, lv_obj_get_x(drawer), edge, wipe_height - 1);
    layer->_clip_area = original;

    // Keep motion visible even as the boundary passes over empty black rows.
    lv_draw_rect_dsc_t line;
    lv_draw_rect_dsc_init(&line);
    line.bg_color = lv_color_hex(0x516a86);
    lv_area_t boundary{0, edge, lv_obj_get_width(unlock_wipe) - 1, edge};
    lv_draw_rect(layer, &line, &boundary);
}

// Copy each composited dirty area of the bare watch face into its snapshot.
void capture_visible_face(lv_event_t* event)
{
    if (!lock_snapshot_dirty || !unlock_lock_pixels || !locked || settling || was_pressed ||
        !lockscreen || rendered_progress != 0 || page != Page::None || overlay_level || overlay_closing)
        return;
    auto* display = static_cast<lv_display_t*>(lv_event_get_target(event));
    if (lv_display_get_render_mode(display) == LV_DISPLAY_RENDER_MODE_PARTIAL ||
        lv_obj_get_child(lv_screen_active(), -1) != lockscreen ||
        !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN))
        return;
    const auto* frame = lv_display_get_buf_active(display);
    const auto& target = unlock_lock_pixels->header;
    if (frame->header.cf != LV_COLOR_FORMAT_RGB565_SWAPPED || target.cf != LV_COLOR_FORMAT_RGB565_SWAPPED ||
        target.w != ui::screen_diameter || target.h != ui::screen_diameter) return;
    lv_area_t coords, dirty;
    lv_obj_get_coords(lockscreen, &coords);
    auto flushed = *static_cast<const lv_area_t*>(lv_event_get_param(event));
    lv_area_move(&flushed, -lv_display_get_offset_x(display), -lv_display_get_offset_y(display));
    if (lv_display_flush_is_last(display)) lock_snapshot_dirty = false;
    if (!lv_area_intersect(&dirty, &flushed, &coords)) return;
    const auto start = perf::now();
    lv_image_cache_drop(unlock_lock_pixels);
    for (int y = dirty.y1; y <= dirty.y2; ++y)
        std::memcpy(unlock_lock_pixels->data + (y - coords.y1) * target.stride + (dirty.x1 - coords.x1) * 2,
                    frame->data + y * frame->header.stride + dirty.x1 * 2, lv_area_get_width(&dirty) * 2);
    perf::sample(perf::snapshot_time, perf::now() - start);
}

void unlock_frame(void*, int32_t value)
{
    unlock_progress = value;
    if (!lockscreen || !drawer) return;
    if (rendered_progress == value) return;
    const int previous = rendered_progress;
    rendered_progress = value;
    auto* screen = lv_screen_active();
    auto* display = lv_obj_get_display(screen);
    const bool intermediate = value > 0 && value < wipe_height;
    const bool cached = unlock_lock_pixels && unlock_drawer_pixels;
    const bool band_only = previous >= 0 && cached &&
        (unlock_wipe || intermediate) &&
        ((previous != 0 && value != 0) || !lock_snapshot_dirty) &&
        ((previous != wipe_height && value != wipe_height) || !drawer_snapshot_dirty);

    // Switching between a page and its identical cached pixels changes only the
    // swept rows. Flush pending layout here so a later pass cannot invalidate
    // the whole new surface. Leave existing dirty regions in LVGL's queue.
    const bool switching = intermediate != (unlock_wipe != nullptr);
    if (band_only && switching) lv_display_enable_invalidation(display, false);
    if (intermediate && cached) {
        if (!unlock_wipe) {
            unlock_wipe = panel(screen, 0, 0, lv_display_get_horizontal_resolution(display), wipe_height, 0);
            lv_obj_add_event_cb(unlock_wipe, draw_unlock_wipe, LV_EVENT_DRAW_MAIN, nullptr);
        }
    } else {
        if (unlock_wipe) {
            lv_obj_delete(unlock_wipe);
            unlock_wipe = nullptr;
        }
        lv_obj_set_flag(drawer, LV_OBJ_FLAG_HIDDEN, value == 0);
        lv_obj_set_flag(lockscreen, LV_OBJ_FLAG_HIDDEN, value == wipe_height);
        // A cache allocation failure still leaves a slide transition.
        lv_obj_set_y(lockscreen, intermediate ? wipe_edge(value) - wipe_height : 0);
    }
    if (band_only && switching) {
        lv_obj_update_layout(screen);
        lv_display_enable_invalidation(display, true);
    }
    if (band_only) {
        const int from = wipe_edge(previous);
        const int to = wipe_edge(value);
        const lv_area_t band{0, std::min(from, to), lv_display_get_horizontal_resolution(display) - 1,
                             std::min(wipe_height - 1, std::max(from, to))};
        lv_obj_invalidate_area(screen, &band);
    }
}

void unlock_finished(lv_anim_t*)
{
    settling = false;
    locked = unlock_progress == 0;
    if (lockscreen) lv_obj_set_flag(lockscreen, LV_OBJ_FLAG_HIDDEN, !locked);
}

void settle_unlock(bool complete)
{
    settling = true;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &unlock_progress);
    const int target = complete ? wipe_height : 0;
    const int remaining = std::abs(target - unlock_progress);
    lv_anim_set_values(&animation, unlock_progress, target);
    const int duration = complete && unlock_motion.velocity > 0
        ? std::clamp(static_cast<int>(remaining / unlock_motion.velocity), std::min(2 * remaining, 180), 280)
        : std::clamp(2 * remaining, 100, 280);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_exec_cb(&animation, unlock_frame);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_completed_cb(&animation, unlock_finished);
    lv_anim_start(&animation);
}

// Refreshes the time/battery card shown by the pull-down overlays: `clock` selects the
// small time-card widgets (level 1) vs. the control-center widgets (level 2).
void refresh_overlay_card(bool clock)
{
    std::time_t now = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&now, &tm);
    const auto power = GetHAL().getBatteryStatus();
    auto* battery_icon = clock ? clock_battery_icon : control_battery_icon;
    ui::battery_level(battery_icon, power.percent, clock ? 0xc3ddff : 0xf3f6fc);
    ui::battery_external_power(battery_icon, power.external_power);
    ui::set_text_fmt(clock ? clock_time : control_time, "%02d:%02d", tm.tm_hour, tm.tm_min);
    ui::set_text_fmt(clock ? clock_battery : control_battery, "%d%%", power.percent);
}

// Arms live finger-tracking for the pull-down overlays, mirroring unlock_dragging but for
// the opposite (downward-open / upward-close) gesture. `opening` picks which direction;
// `pull_surface`/`pull_height` are fixed for the rest of this press so the move-phase and
// release-time settle just read them back.
void arm_pull_drag(bool opening)
{
    pull_dragging = true;
    pull_closing = !opening;
    const int target_level = opening ? std::min(2, overlay_level + 1) : 0;
    pull_surface = (target_level == 2 || overlay_level == 2) ? controls : clock_panel;
    pull_height = surface_height(pull_surface);
    lv_anim_delete(clock_panel, overlay_position);
    lv_anim_delete(controls, overlay_position);
    if (opening) {
        present_surface(pull_surface);
        refresh_overlay_card(target_level == 1);
    } else if (pull_surface == controls) {
        // Closing slides the cached pixels; a sheet that was never captured
        // simply keeps the live-render path.
        start_controls_slide();
    } else if (overlay_face_cached) {
        // Restore the face as the time card starts sliding out, while its
        // covered rows can still be painted from the same cached image.
        set_backdrop(false);
    }
}

// Finishes a pull-down drag: continues from wherever the finger left the surface, with a
// short duration shaped by drag distance and release velocity (same formula as settle_unlock).
void settle_pull(int target_level)
{
    // The dragged sheet either is the one shown at `target_level` or retreats.
    const bool shown = target_level == (pull_surface == controls ? 2 : 1);
    present_surface(pull_surface);
    const int current_y = surface_y(pull_surface);
    const int target_y = shown ? 0 : -pull_height;
    const int remaining = std::abs(target_y - current_y);
    const int duration = pull_motion.velocity != 0
        ? std::clamp(static_cast<int>(remaining / std::abs(pull_motion.velocity)), std::min(2 * remaining, 120), 200)
        : std::clamp(2 * remaining, 80, 200);
    last_second = UINT32_MAX;
    // Only a real "was showing, now hiding" transition should block new touches/snapshots
    // while it finishes; reverting an aborted open-drag back to 0 shouldn't.
    overlay_closing = target_level == 0 && overlay_level != 0;
    if (target_level == 1) clock_shown_at = lv_tick_get();
    overlay_level = target_level;
    animate_surface(pull_surface, target_y, duration);
    if (target_level) update_time();
}

// The analog face is drawn dimmed on black to limit AMOLED power draw and wear.
// Scaling each color gives the pixels of a translucent layer over black without
// rendering the whole dial off-screen for every hand movement.
constexpr uint32_t dial_opacity = 90;
constexpr uint32_t dial_color(uint32_t color)
{
    return ((color >> 16 & 0xff) * dial_opacity / 100) << 16 |
           ((color >> 8 & 0xff) * dial_opacity / 100) << 8 |
           (color & 0xff) * dial_opacity / 100;
}
constexpr uint32_t dial_battery_color = dial_color(0x617184);

// LVGL sizes a line from its local origin to its furthest point. Anchor each
// line at its own bounds so moving a hand only invalidates its old and new area.
void position_dial_line(lv_obj_t* line, std::array<lv_point_precise_t, 2>& points,
                        lv_point_precise_t start, lv_point_precise_t end)
{
    const auto x = std::min(start.x, end.x);
    const auto y = std::min(start.y, end.y);
    // Include the endpoint in the line's bounds, including horizontal and
    // vertical lines, whose content height or width would otherwise be zero.
    lv_obj_set_size(line, std::max(start.x, end.x) - x + 1, std::max(start.y, end.y) - y + 1);
    lv_obj_set_pos(line, x, y);
    points = {{{start.x - x, start.y - y}, {end.x - x, end.y - y}}};
    lv_line_set_points(line, points.data(), points.size());
}

void create_dial()
{
    dial = panel(lock_content, 0, 0, 466, 466);
    // Only the 4 cardinal hours are drawn, to keep the static luminous area small on AMOLED.
    for (int hour : {12, 3, 6, 9}) {
        const double angle = hour * 3.141592653589793 / 6;
        text(dial, std::to_string(hour).c_str(),
             209 + std::lround(166 * std::sin(angle)),
             211 - std::lround(166 * std::cos(angle)), 48, &toolbox_misans_36, dial_color(0x8e9cad));
    }
    for (int tick = 0; tick < 60; ++tick) {
        const double angle = tick * 3.141592653589793 / 30;
        const bool hour_mark = tick % 5 == 0;
        const int inner = hour_mark ? 193 : 205;
        const lv_point_precise_t start{
            static_cast<lv_value_precise_t>(233 + 214 * std::sin(angle)),
            static_cast<lv_value_precise_t>(233 - 214 * std::cos(angle))};
        const lv_point_precise_t end{
            static_cast<lv_value_precise_t>(233 + inner * std::sin(angle)),
            static_cast<lv_value_precise_t>(233 - inner * std::cos(angle))};
        auto* line = lv_line_create(dial);
        position_dial_line(line, tick_points[tick], start, end);
        // Minute ticks stay low luminance to reduce static emissive stress; hour ticks a bit brighter.
        lv_obj_set_style_line_color(line, lv_color_hex(dial_color(hour_mark ? 0x566477 : 0x293441)), 0);
        lv_obj_set_style_line_width(line, hour_mark ? 3 : 2, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }
    // A brighter accent marker layered on the 12 o'clock tick.
    auto* accent_tick = lv_line_create(dial);
    position_dial_line(accent_tick, tick_points[0], {233, 19}, {233, 40});
    lv_obj_set_style_line_color(accent_tick, lv_color_hex(dial_color(0x8fb7df)), 0);
    lv_obj_set_style_line_width(accent_tick, 3, 0);
    lv_obj_set_style_line_rounded(accent_tick, true, 0);
    // The weekday is Chinese text, so it needs a font with CJK glyphs.
    dial_day = text(dial, "", 153, 107, 160, &toolbox_misans_20, dial_color(0x738499));
    dial_date = text(dial, "", 153, 136, 160, &toolbox_misans_32, dial_color(0xa6b7c7));
    // Low-luminance battery glyph instead of a large static circular progress meter.
    dial_battery_icon = ui::battery(dial, dial_battery_color, 0x91dcb5);
    lv_obj_set_pos(dial_battery_icon, 174, 312);
    dial_battery = text(dial, "", 222, 316, 60, &toolbox_misans_20, dial_color(0x8a9bac));
    const uint32_t hand_colors[] = {dial_color(0xdce7f0), dial_color(0xc3d2e0), dial_color(0x8ebfef)};
    for (int index = 0; index < 3; ++index) {
        hands[index] = lv_line_create(dial);
        lv_obj_set_style_line_color(hands[index], lv_color_hex(hand_colors[index]), 0);
        lv_obj_set_style_line_width(hands[index], index == 0 ? 12 : index == 1 ? 8 : 2, 0);
        lv_obj_set_style_line_rounded(hands[index], true, 0);
    }
    panel(dial, 223, 223, 20, 20, dial_color(0xdce7f0), 10);
    panel(dial, 227, 227, 12, 12, dial_color(0x182735), 6);
    panel(dial, 230, 230, 6, 6, dial_color(0x8ebfef), 3);
}

void update_time()
{
    if (overlay_level) refresh_overlay_card(overlay_level == 1);
    // Hand and time-label updates repaint large regions of the watch face, so
    // they wait until the pull-down overlays are done: the face is dimmed or
    // covered while a sheet is showing, moving or settling. The closing
    // animation refreshes the live hands as the face reappears.
    if (!lockscreen || !locked || settling || was_pressed || page != Page::None ||
        overlay_level || overlay_closing || pull_dragging) return;
    std::time_t now = std::time(nullptr);
    std::tm tm = {};
    localtime_r(&now, &tm);
    const auto power = GetHAL().getBatteryStatus();
    const int battery = power.percent;
    auto redraw_if_changed = [](bool changed) { lock_snapshot_dirty |= changed; };
    const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    if (!analog) {
        ui::battery_level(lock_battery, battery, 0xb9c7db);
        redraw_if_changed(ui::battery_external_power(lock_battery, power.external_power));
        redraw_if_changed(ui::set_text_fmt(digital, "%02d:%02d", tm.tm_hour, tm.tm_min));
        redraw_if_changed(ui::set_text_fmt(date_label, "%d月%d日 · 星期%s", tm.tm_mon + 1, tm.tm_mday, weekdays[tm.tm_wday]));
        redraw_if_changed(ui::set_text_fmt(status, "%d%%", battery));
        const auto opa = GetHAL().getWifiStatus().connected ? LV_OPA_COVER : LV_OPA_30;
        if (lv_obj_get_style_opa(lock_wifi, LV_PART_MAIN) != opa) {
            lv_obj_set_style_opa(lock_wifi, opa, 0);
            lock_snapshot_dirty = true;
        }
        return;
    }
    redraw_if_changed(ui::set_text_fmt(dial_day, "周%s", weekdays[tm.tm_wday]));
    redraw_if_changed(ui::set_text_fmt(dial_date, "%d", tm.tm_mday));
    if (dial_battery_percent != battery) {
        dial_battery_percent = battery;
        ui::battery_level(dial_battery_icon, battery, dial_battery_color);
        lock_snapshot_dirty = true;
    }
    redraw_if_changed(ui::battery_external_power(dial_battery_icon, power.external_power));
    redraw_if_changed(ui::set_text_fmt(dial_battery, "%d%%", battery));
    const int steps[] = {(tm.tm_hour % 12) * 60 + tm.tm_min, tm.tm_min * 60 + tm.tm_sec, tm.tm_sec};
    const double degrees_per_step[] = {.5, .1, 6};
    const int lengths[] = {102, 148, 187};
    for (int i = 0; i < 3; ++i) {
        if (hand_steps[i] == steps[i]) continue;
        hand_steps[i] = steps[i];
        const double angle = steps[i] * degrees_per_step[i] * 3.141592653589793 / 180;
        const int tail = i == 2 ? 30 : 13;
        position_dial_line(hands[i], hand_points[i],
            {static_cast<lv_value_precise_t>(233 - tail * std::sin(angle)),
             static_cast<lv_value_precise_t>(233 + tail * std::cos(angle))},
            {static_cast<lv_value_precise_t>(233 + lengths[i] * std::sin(angle)),
             static_cast<lv_value_precise_t>(233 - lengths[i] * std::cos(angle))});
        lock_snapshot_dirty = true;
    }
}

void tick(lv_timer_t*)
{
    if (lockscreen && locked && !settling && !was_pressed && !overlay_level && !overlay_closing &&
        lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(shift_tick) >= 300000) {
        shift_tick = lv_tick_get();
        lock_snapshot_dirty = true;
        lv_obj_set_style_translate_x(lock_content, static_cast<int>(lv_rand(0, 8)) - 4, 0);
        lv_obj_set_style_translate_y(lock_content, static_cast<int>(lv_rand(0, 8)) - 4, 0);
    }
    if (overlay_level == 1 && !was_pressed && lv_tick_elaps(clock_shown_at) >= 1500) show_overlay(0);
    if (pending_brightness && runtime().post({CommandType::Brightness, desired_brightness, save_brightness})) {
        pending_brightness = save_brightness = false;
    }
    if (pending_volume && runtime().post({CommandType::Volume, desired_volume, save_volume})) {
        pending_volume = save_volume = false;
    }
    if (pending_ble && runtime().post({CommandType::Ble, desired_ble, true})) pending_ble = false;
    if (pending_provision && runtime().post({CommandType::Provision, desired_provision, true})) pending_provision = false;
    if (pending_face && runtime().post({CommandType::Face, analog, true})) pending_face = false;
    if (pending_wifi && runtime().post({CommandType::Wifi, desired_wifi, true})) pending_wifi = false;
    if (drawer_snapshot_dirty && locked && !was_pressed && !settling && page == Page::None && overlay_level < 2) {
        refresh_snapshot(drawer, unlock_drawer_pixels);
        drawer_snapshot_dirty = false;
    }
    auto second = lv_tick_get() / 1000;
    if (second == last_second) return;
    last_second = second;
    update_time();
    if (lockscreen && locked && !settling && !was_pressed && page == Page::None &&
        overlay_level < 2 && lock_snapshot_dirty && !unlock_lock_pixels) {
        refresh_snapshot(lockscreen, unlock_lock_pixels);
        lock_snapshot_dirty = false;
    }
    const bool ble_on = ble_enabled();

    if (ble_status) ui::set_text(ble_status, ble_error() == ESP_ERR_NO_MEM ? "内部内存不足" : ble_error() != ESP_OK ? "BLE 开启失败" : ble_on ? "BLE 已开启" : "BLE 已关闭");
    auto network = GetHAL().getWifiStatus();
    if (network.configMode && !provisioning_visible) {
        show_overlay(0);
        shell_open_settings();
        render_page(Page::Ap);
    }
    provisioning_visible = network.configMode;
    if (page == Page::Wifi || page == Page::Ap) {
        if (page == Page::Ap && !network.configMode) render_page(Page::Wifi);
        if (page == Page::Ap) {
            ui::set_text(network_name, network.apSsid.c_str());
            ui::set_text(network_text, network.apUrl.c_str());
        } else {
            const char* state = network.connected ? "已连接" : network.mode == Hal::WifiMode::Error ? "连接失败" : network.enabled ? "正在连接" : "Wi-Fi 已关闭";
            ui::set_text(network_name, network.ssid.c_str());
            ui::set_text_fmt(network_text, "%s\n%s", state, network.ipAddress.c_str());
            lv_obj_set_state(wifi_switch, LV_STATE_CHECKED, network.enabled);
        }
    }
    if (overlay_level != 2) return;
    for (auto* tile : {wifi_button, ble_button}) {
        const bool on = tile == wifi_button ? GetHAL().isWifiEnabled() : ble_on;
        const auto bg = lv_color_hex(on ? accent : 0x202631);
        auto* content = lv_obj_get_child(tile, 1);
        const auto fg = lv_color_hex(on ? 0x081627 : 0xf3f6fc);
        auto* caption = lv_obj_get_child(content, 1);
        if (!lv_color_eq(lv_obj_get_style_bg_color(tile, LV_PART_MAIN), bg)) lv_obj_set_style_bg_color(tile, bg, 0);
        if (!lv_color_eq(lv_obj_get_style_text_color(caption, LV_PART_MAIN), fg)) {
            lv_obj_set_style_text_color(caption, fg, 0);
            lv_obj_set_style_image_recolor(lv_obj_get_child(content, 0), fg, 0);
        }
    }
    if (lv_indev_get_state(GetHAL().lvTouchpad) == LV_INDEV_STATE_RELEASED) {
        if (!pending_brightness) {
            const int value = GetHAL().getBackLightBrightness();
            if (lv_slider_get_value(brightness) != value) lv_slider_set_value(brightness, value, LV_ANIM_OFF);
            ui::set_text_fmt(brightness_value, "%d", value);
        }
        if (!pending_volume) {
            const int value = GetHAL().getSpeakerVolume();
            if (lv_slider_get_value(volume) != value) lv_slider_set_value(volume, value, LV_ANIM_OFF);
            ui::set_text_fmt(volume_value, "%d", value);
        }
    }
}

void slider_event(lv_event_t* event)
{
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool save = lv_event_get_code(event) == LV_EVENT_RELEASED;
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED && !save) return;
    lv_label_set_text_fmt(static_cast<lv_obj_t*>(lv_event_get_user_data(event)), "%d", static_cast<int>(lv_slider_get_value(slider)));
    if (lv_obj_get_user_data(slider)) {
        desired_brightness = lv_slider_get_value(slider);
        pending_brightness = true;
        save_brightness |= save;
    } else {
        desired_volume = lv_slider_get_value(slider);
        pending_volume = true;
        save_volume |= save;
    }
}

lv_obj_t* slider_card(lv_obj_t* parent, bool is_brightness, int top, lv_obj_t** output)
{
    auto* card = panel(parent, 69, top, 328, 66, 0x202631, 25);
    icon(card, is_brightness ? &toolbox_icon_sun_28 : &toolbox_icon_sound_28, 20, 19);
    *output = text(card, "", 274, 21, 40, &toolbox_misans_18);
    auto* slider = lv_slider_create(card);
    lv_obj_remove_style_all(slider);
    lv_obj_set_pos(slider, 66, 29);
    lv_obj_set_size(slider, 190, 8);
    lv_slider_set_range(slider, is_brightness ? 10 : 0, 100);
    const int value = is_brightness ? GetHAL().getBackLightBrightness() : GetHAL().getSpeakerVolume();
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_label_set_text_fmt(*output, "%d", value);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4d5664), LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 20);
    lv_obj_set_user_data(slider, reinterpret_cast<void*>(static_cast<uintptr_t>(is_brightness ? 1 : 0)));
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_ALL, *output);
    return slider;
}

void return_to_menu(lv_event_t*) { render_page(Page::Settings); }

void render_page(Page target)
{
    page = target;
    set_overlay_face(false);
    network_name = network_text = wifi_switch = ble_status = nullptr;
    lv_obj_clean(settings);
    lv_obj_remove_flag(settings, LV_OBJ_FLAG_HIDDEN);
    last_second = UINT32_MAX;
    if (target == Page::Settings) {
        ui::heading(settings, "设置", [](lv_event_t*) { shell_close_settings(); });
        auto* rows = ui::menu_list(settings);
        const char* names[] = {"Wi-Fi", "Bluetooth", "屏幕", "声音", "系统信息", "表盘样式", "手机配置", "自动返回 / 关机"};
        const lv_image_dsc_t* symbols[] = {&toolbox_icon_wifi_28, &toolbox_icon_ble_28, &toolbox_icon_sun_28, &toolbox_icon_sound_28, &toolbox_icon_box_28, &toolbox_icon_clock_28, &toolbox_icon_phone_28, &toolbox_icon_power_28};
        const Page pages[] = {Page::Wifi, Page::Ble, Page::Display, Page::Sound, Page::Info, Page::Faces, Page::Phone, Page::Power};
        for (int index = 0; index < 8; ++index) {
            auto* row = ui::menu_row(rows, names[index], 0, index * ui::menu_pitch, nullptr, symbols[index], true).object;
            lv_obj_add_event_cb(row, [](lv_event_t* event) {
                render_page(static_cast<Page>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))))));
            }, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(pages[index])));
        }
        ui::cache_scroll_rows(rows);
        return;
    }
    const char* title = target == Page::Wifi ? "Wi-Fi" : target == Page::Ap ? "连接手机" : target == Page::Faces ? "表盘样式" : target == Page::Ble ? "Bluetooth" : target == Page::Display ? "屏幕" : target == Page::Sound ? "声音" : "系统信息";
    ui::heading(settings, target == Page::Power ? "自动返回 / 关机" : target == Page::Phone ? "手机配置" : title, return_to_menu);
    if (target == Page::Power) {
        text(settings, "返回主页时长", 69, 145, 328, &toolbox_misans_23);
        auto* home = lv_dropdown_create(settings);
        lv_obj_remove_style_all(home);
        lv_obj_set_pos(home, 89, 185);
        lv_obj_set_size(home, 288, 50);
        lv_dropdown_set_options(home, "30s\n60s\n90s\n永不");
        lv_dropdown_set_selected(home, home_seconds == 0 ? 3 : home_seconds / 30 - 1);
        text(settings, "自动关机时长", 69, 265, 328, &toolbox_misans_23);
        auto* power = lv_dropdown_create(settings);
        lv_obj_remove_style_all(power);
        lv_obj_set_pos(power, 89, 305);
        lv_obj_set_size(power, 288, 50);
        lv_dropdown_set_options(power, home_seconds ? "5min\n10min\n20min\n30min" : "永不");
        lv_dropdown_set_selected(power, !home_seconds ? 0 : power_seconds == 300 ? 0 : power_seconds == 600 ? 1 : power_seconds == 1200 ? 2 : 3);
        lv_obj_set_state(power, LV_STATE_DISABLED, !home_seconds);
        for (auto* dropdown : {home, power}) {
            lv_obj_set_style_text_font(dropdown, &toolbox_misans_20, 0);
            lv_obj_set_style_text_color(dropdown, lv_color_hex(0xf3f6fc), 0);
            lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x181d25), 0);
            lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dropdown, 22, 0);
            lv_obj_set_style_pad_hor(dropdown, 20, 0);
            lv_obj_set_style_pad_ver(dropdown, 13, 0);
            lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x283950), LV_STATE_PRESSED);
            lv_obj_set_style_opa(dropdown, LV_OPA_40, LV_STATE_DISABLED);
            lv_dropdown_set_symbol(dropdown, LV_SYMBOL_DOWN);
            lv_obj_set_style_text_font(dropdown, LV_FONT_DEFAULT, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(dropdown, lv_color_hex(accent), LV_PART_INDICATOR);
            auto* list = lv_dropdown_get_list(dropdown);
            lv_obj_remove_style_all(list);
            lv_obj_set_style_text_font(list, &toolbox_misans_20, 0);
            lv_obj_set_style_text_color(list, lv_color_hex(0xf3f6fc), 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(0x202631), 0);
            lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(list, 20, 0);
            lv_obj_set_style_pad_all(list, 14, 0);
            lv_obj_set_style_text_line_space(list, 16, 0);
            lv_obj_set_style_max_height(list, 200, 0);
            lv_obj_set_style_bg_color(list, lv_color_hex(0x344861), LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_style_text_color(list, lv_color_hex(accent), LV_PART_SELECTED | LV_STATE_CHECKED);
            lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
        }
        lv_obj_add_event_cb(home, [](lv_event_t* e) {
            int selected = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));
            int seconds = selected == 3 ? 0 : (selected + 1) * 30;
            if (runtime().post({CommandType::HomeTimeout, seconds, true})) home_seconds = seconds;
            shell_note_activity();
            render_page(Page::Power);
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_add_event_cb(power, [](lv_event_t* e) {
            const int durations[] = {300, 600, 1200, 1800};
            int seconds = durations[lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)))];
            if (runtime().post({CommandType::PowerTimeout, seconds, true})) power_seconds = seconds;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        return;
    }
    if (target == Page::Phone) {
        text(settings, "开启手机配置", 69, 160, 240, &toolbox_misans_23);
        auto* toggle = lv_switch_create(settings);
        lv_obj_set_pos(toggle, 320, 160);
        lv_obj_set_state(toggle, LV_STATE_CHECKED, GetHAL().getWifiStatus().configMode);
        lv_obj_add_event_cb(toggle, [](lv_event_t* e) {
            desired_provision = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_CHECKED);
            pending_provision = true;
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        text(settings, "打开配置热点\n已保存的 Wi-Fi 配置会保留", 69, 235, 328, &toolbox_misans_20);
        return;
    }
    if (target == Page::Wifi || target == Page::Ble) {
        auto* row = panel(settings, 69, ui::content_top, 328, 62, 0x181d25, 22);
        auto* content = ui::row(row, 19, 0, 290, 62);
        auto* name = ui::row_text(content, target == Page::Wifi ? "Wi-Fi" : "BLE", &toolbox_misans_23);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
        auto* toggle = lv_switch_create(content);
        lv_obj_set_size(toggle, 56, 32);
        lv_obj_set_style_bg_color(toggle, lv_color_hex(0x7eaff4), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_state(toggle, LV_STATE_CHECKED, target == Page::Wifi ? GetHAL().isWifiEnabled() : ble_enabled());
        lv_obj_add_event_cb(toggle, [](lv_event_t* event) {
            const bool enabled = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
            if (page == Page::Wifi) { desired_wifi = enabled; pending_wifi = true; }
            else { desired_ble = enabled; pending_ble = true; }
        }, LV_EVENT_VALUE_CHANGED, nullptr);
        if (target == Page::Ble) {
            ble_status = text(settings, "", 83, 220, 300, &toolbox_misans_23, 0x98a4b7);
            return;
        }
        wifi_switch = toggle;
        auto* network = panel(settings, 69, 218, 328, 120, 0x181d25, 24);
        network_name = text(network, "", 20, 16, 288, &toolbox_misans_23);
        lv_label_set_long_mode(network_name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(network_name, LV_TEXT_ALIGN_LEFT, 0);
        network_text = text(network, "", 20, 54, 288, &toolbox_misans_20, 0xa8b7ca);
        lv_obj_set_style_text_line_space(network_text, 8, 0);
        lv_label_set_long_mode(network_text, LV_LABEL_LONG_DOT);
        lv_obj_set_height(network_text, 60);
        lv_obj_set_style_text_align(network_text, LV_TEXT_ALIGN_LEFT, 0);
        button(settings, "重新配网", 69, 354, 328, 52, [](lv_event_t*) {
            if (GetHAL().getWifiStatus().configMode) render_page(Page::Ap);
            else { desired_provision = true; pending_provision = true; }
        }, 0x283950);
    } else if (target == Page::Ap) {
        text(settings, "手机连接以下热点", 66, 144, 334, &toolbox_misans_20);
        auto* card = panel(settings, 66, 182, 334, 68, 0x181d25, 24);
        network_name = text(card, "", 0, 20, 334, &toolbox_misans_23);
        text(settings, "在浏览器打开", 66, 269, 334, &toolbox_misans_20);
        network_text = text(settings, "", 66, 301, 334, &toolbox_misans_20, accent);
        button(settings, "退出配网", 151, 346, 164, 52, [](lv_event_t*) {
            desired_provision = false; pending_provision = true;
        }, 0x283950);
    } else if (target == Page::Faces) {
        button(settings, "数字表盘", 69, ui::content_top, 328, 82, [](lv_event_t*) { set_analog(false); pending_face = true; render_page(Page::Settings); }, 0x181d25);
        button(settings, "指针表盘", 69, 234, 328, 82, [](lv_event_t*) { set_analog(true); pending_face = true; render_page(Page::Settings); }, 0x181d25);
    } else if (target == Page::Display || target == Page::Sound) {
        lv_obj_t* value;
        slider_card(settings, target == Page::Display, 200, &value);
    } else {
        text(settings, "ToolBox OS 1.2\nM5Stack StopWatch\nESP32-S3", 69, 150, 328, &toolbox_misans_23);
    }
}
}

void shell_init()
{
    lv_display_add_event_cb(lv_display_get_default(), capture_visible_face, LV_EVENT_FLUSH_START, nullptr);
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lv_screen_active(), LV_SCROLLBAR_MODE_OFF);
    Settings preferences("toolbox", false);
    analog = preferences.GetInt("analog", 0) != 0;
    home_seconds = preferences.GetInt("home_secs", 60);
    power_seconds = preferences.GetInt("power_secs", 600);
    if (home_seconds != 0 && home_seconds != 30 && home_seconds != 60 && home_seconds != 90) home_seconds = 60;
    if (power_seconds != 300 && power_seconds != 600 && power_seconds != 1200 && power_seconds != 1800) power_seconds = 600;
    shell_note_activity();
    settings = ui::round_page(lv_layer_top());
    lv_obj_add_flag(settings, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
    overlay = ui::round_page(lv_layer_top());
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(overlay, draw_overlay_face, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_flag(overlay, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN));
    clock_panel = panel(overlay, 113, 0, 240, 132, 0x202936, 44);
    // Square top corners retain the hanging-tab shape of the prototype.
    panel(clock_panel, 0, 0, 240, 44, 0x202936);
    clock_time = text(clock_panel, "", 0, 26, 240, &toolbox_misans_semibold_36, 0xc3ddff);
    auto* clock_row = ui::row(clock_panel, 0, 77, 240, 28, 8);
    clock_battery_icon = ui::battery(clock_row, 0xc3ddff, 0xafe4ca);
    clock_battery = ui::row_text(clock_row, "82%", &toolbox_misans_20, 0xc3ddff);
    controls = panel(overlay, 0, 0, 466, 466);
    control_time = text(controls, "", 123, 45, 220, &toolbox_misans_24);
    wifi_button = button(controls, "Wi-Fi", 84, 96, 142, 86, [](lv_event_t*) {
        desired_wifi = pending_wifi ? !desired_wifi : !GetHAL().isWifiEnabled(); pending_wifi = true;
    }, accent, &toolbox_misans_18);
    ble_button = button(controls, "BLE", 240, 96, 142, 86, [](lv_event_t*) {
        desired_ble = pending_ble ? !desired_ble : !ble_enabled(); pending_ble = true;
    }, 0x202631, &toolbox_misans_18);
    for (auto* tile : {wifi_button, ble_button}) {
        lv_obj_add_flag(lv_obj_get_child(tile, 0), LV_OBJ_FLAG_HIDDEN);
        auto* content = ui::row(tile, 0, 0, 142, 86, 12);
        icon(content, tile == wifi_button ? &toolbox_icon_wifi_28 : &toolbox_icon_ble_28, 0, 0);
        ui::row_text(content, tile == wifi_button ? "Wi-Fi" : "BLE", &toolbox_misans_18);
        lv_obj_set_style_radius(tile, 28, 0);
    }
    brightness = slider_card(controls, true, 199, &brightness_value);
    volume = slider_card(controls, false, 278, &volume_value);
    auto* bottom = ui::row(controls, 0, 354, 466, 52, 15);
    control_battery_icon = ui::battery(bottom);
    control_battery = ui::row_text(bottom, "82%", &toolbox_misans_22);
    icon_button(bottom, &toolbox_icon_settings_28, 0, 0, 58, 52, [](lv_event_t*) {
        show_overlay(0); shell_open_settings();
    });
    controls_motion = panel(overlay, 0, 0, 466, 466);
    lv_obj_add_flag(controls_motion, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(controls_motion, draw_control_sheet, LV_EVENT_DRAW_MAIN, nullptr);
    lv_display_add_event_cb(lv_display_get_default(), capture_control_center, LV_EVENT_FLUSH_START, nullptr);
    lv_timer_create(tick, 20, nullptr);
}

void shell_open_settings(std::function<void()> on_close)
{
    if (page == Page::None) settings_closed = std::move(on_close);
    render_page(Page::Settings);
}

void shell_close_settings()
{
    if (page == Page::None && lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN)) return;
    page = Page::None;
    last_second = UINT32_MAX;
    network_text = ble_status = wifi_switch = nullptr;
    lv_obj_add_flag(settings, LV_OBJ_FLAG_HIDDEN);
    auto callback = std::move(settings_closed);
    settings_closed = {};
    if (callback) callback();
}

void shell_key_unlock()
{
    if (!shell_is_locked() || page != Page::None || overlay_level || overlay_closing || settling || was_pressed) return;
    shell_note_activity();
    unlock_motion.velocity = 0;
    settle_unlock(true);
}

void shell_lock()
{
    resume_drawer = false;
    if (!lockscreen || (locked && !settling && rendered_progress == 0)) return;
    lv_anim_delete(&unlock_progress, unlock_frame);
    settling = false;
    locked = true;
    const bool changed = rendered_progress != 0;
    lv_obj_remove_flag(lockscreen, LV_OBJ_FLAG_HIDDEN);
    unlock_frame(nullptr, 0);
    if (changed) {
        update_time();
        // Defer both snapshots to the existing lazy paths (tick()'s drawer refresh,
        // capture_visible_face()'s free post-flush memcpy) instead of rendering two
        // full offscreen trees synchronously in the frame the user expects an
        // instant return to the watch face.
        drawer_snapshot_dirty = true;
        lock_snapshot_dirty = true;
    }
}

void shell_attach(lv_obj_t* root)
{
    drawer = root;
    drawer_snapshot_dirty = false;
    rendered_progress = -1;
    hand_steps.fill(-1);
    dial_battery_percent = -1;
    lockscreen = ui::round_page(lv_screen_active());
    lock_content = ui::round_page(lockscreen);
    digital = text(lock_content, "", 0, 150, 466, &toolbox_misans_98);
    lv_obj_set_style_text_letter_space(digital, -5, 0);
    date_label = text(lock_content, "", 33, 280, 400, &toolbox_misans_25, 0xaeb9c9);
    lock_status_row = ui::row(lock_content, 0, 64, 466, 32, 13);
    lock_wifi = icon(lock_status_row, &toolbox_icon_wifi_28, 0, 0, 0xb9c7db);
    lock_battery = ui::battery(lock_status_row, 0xb9c7db);
    status = ui::row_text(lock_status_row, "82%", &toolbox_misans_23, 0xb9c7db);
    auto* unlock_button = icon_button(lock_content, &toolbox_icon_up_28, 201, 348, 64, 56, [](lv_event_t*) { settle_unlock(true); });
    lv_obj_set_style_bg_opa(unlock_button, LV_OPA_TRANSP, LV_PART_MAIN);
    create_dial();
    lv_obj_set_style_text_opa(digital, LV_OPA_70, 0);
    set_analog(analog);
    locked = true;
    settling = false;
    unlock_frame(nullptr, 0);
    update_time();
    if (!resume_drawer) {
        GetHAL().presentBootHome();
        refresh_snapshot(drawer, unlock_drawer_pixels);
        refresh_snapshot(lockscreen, unlock_lock_pixels);
        lock_snapshot_dirty = false;
    }
    if (resume_drawer) {
        locked = false;
        unlock_frame(nullptr, wipe_height);
        resume_drawer = false;
    }
}

void shell_detach()
{
    lv_anim_delete(&unlock_progress, unlock_frame);
    if (unlock_wipe) lv_obj_delete(unlock_wipe);
    unlock_wipe = nullptr;
    set_overlay_face(false);
    destroy_snapshot(unlock_lock_pixels);
    destroy_snapshot(unlock_drawer_pixels);
    if (lockscreen) lv_obj_delete(lockscreen);
    lockscreen = lock_content = drawer = nullptr;
    drawer_snapshot_dirty = false;
    settling = false;
}

void shell_drawer_settled(lv_obj_t* root)
{
    if (root == drawer && locked) drawer_snapshot_dirty = true;
}

bool shell_is_locked() { return lockscreen && locked; }
void shell_resume_drawer() { resume_drawer = true; }
void shell_note_activity() { activity_tick = lv_tick_get(); }
uint32_t shell_idle_ms() { return lv_tick_get() - activity_tick.load(); }
int shell_home_timeout() { return home_seconds.load(); }
int shell_power_timeout() { return home_seconds ? power_seconds.load() : 0; }

void shell_cancel_touch()
{
    const bool was_pulling = pull_dragging;
    was_pressed = captured = unlock_dragging = pull_dragging = false;
    unlock_motion.velocity = 0;
    if (shell_is_locked() && rendered_progress > 0) {
        unlock_progress = rendered_progress;
        settle_unlock(false);
    }
    if (was_pulling) settle_pull(overlay_level);
}

void shell_dismiss_touch_overlays()
{
    shell_cancel_touch();
    if (overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) {
        lv_anim_delete(clock_panel, overlay_position);
        lv_anim_delete(controls, overlay_position);
        overlay_level = 0;
        overlay_settled(nullptr);
    }
    if (settings) shell_close_settings();
}

bool shell_touch(bool pressed, int x, int y, uint32_t sample_tick, bool draw)
{
    if (pressed) shell_note_activity();
    const uint32_t now = sample_tick == UINT32_MAX ? lv_tick_get() : sample_tick;
    if (!overlay) return false;
    if (overlay_closing) return true;
    if (settling) { was_pressed = pressed; return true; }
    if (pressed && !was_pressed) {
        unlock_dragging = false;
        pull_dragging = false;
        unlock_motion.reset(y, now);
        pull_motion.reset(y, now);
        start_x = last_x = x;
        start_y = last_y = y;
        captured = (shell_is_locked() && page == Page::None && overlay_level == 0) || y < 64 || overlay_level == 1;
    }
    if (pressed) {
        last_x = x;
        last_y = y;
        if (shell_is_locked() && page == Page::None && overlay_level == 0) {
            if (start_y - y > 6 && start_y - y > std::abs(x - start_x)) unlock_dragging = true;
            if (unlock_dragging) {
                unlock_motion.sample(y, now);
                const int progress = std::clamp(start_y - y, 0, wipe_height);
                if (draw) unlock_frame(nullptr, progress);
                else unlock_progress = progress;
            } else {
                unlock_motion.y = y;
                unlock_motion.tick = now;
            }
        }
        // Mirror image of the unlock-swipe arming above: downward motion opens the pull-down
        // overlays further, upward motion closes them. A bigger deadzone guards the controls
        // panel's own sliders/switches, same as the single-case check this replaces used to.
        if (!unlock_dragging && !pull_dragging) {
            const bool open_eligible = overlay_level != 2 && (start_y < 64 || overlay_level == 1);
            const bool close_eligible = overlay_level >= 1;
            const int close_threshold = overlay_level == 2 ? 35 : 6;
            if (open_eligible && y - start_y > 6 && y - start_y > std::abs(x - start_x)) {
                arm_pull_drag(true);
            } else if (close_eligible && start_y - y > close_threshold && start_y - y > std::abs(x - start_x)) {
                arm_pull_drag(false);
                captured = true;
            }
        }
        if (pull_dragging) {
            pull_motion.sample(y, now);
            const int progress = std::clamp(pull_closing ? start_y - y : y - start_y, 0, pull_height);
            const int dragged_y = pull_closing ? -progress : progress - pull_height;
            if (draw) move_surface(pull_surface, dragged_y);
        }
    }
    const bool consume = captured;
    if (!pressed && was_pressed && !settling) {
        const int previous_level = overlay_level;
        if (pull_dragging) {
            const int dy_total = last_y - start_y;
            if ((now - pull_motion.tick) > 100) pull_motion.velocity = 0;
            // The tracker reports upward motion as positive.
            const bool flick_open = pull_motion.velocity <= -0.30f;
            const bool flick_close = pull_motion.velocity >= 0.30f;
            int target_level = overlay_level;
            if (pull_closing) {
                if (flick_close || -dy_total > pull_height / 2) target_level = 0;
            } else if (flick_open || dy_total > pull_height / 2) {
                target_level = std::min(2, overlay_level + 1);
            }
            settle_pull(target_level);
            pull_dragging = false;
        } else {
            const int dy = last_y - start_y;
            if (std::abs(dy) > std::abs(last_x - start_x)) {
                if (dy > 45 && (start_y < 64 || overlay_level == 1)) show_overlay(std::min(2, overlay_level + 1));
                else if (dy < -45 && overlay_level) show_overlay(0);
            }
        }
        if (shell_is_locked() && previous_level == 0 && unlock_dragging) {
            if (unlock_progress == 0) {
                unlock_frame(nullptr, 0);
            } else {
                if ((now - unlock_motion.tick) > 100) unlock_motion.velocity = 0;
                const int distance = start_y - last_y;
                const bool flick = distance >= 16 && unlock_motion.velocity >= 0.30f;
                settle_unlock(unlock_motion.velocity > -0.15f && (distance >= 64 || flick));
            }
        }
    }
    was_pressed = pressed;
    if (!pressed) captured = false;
    return consume;
}
}


