#include "ui.h"
#include <src/core/lv_obj_draw_private.h>
#include <src/core/lv_obj_event_private.h>
#include <src/misc/lv_area_private.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
extern "C" {
LV_IMAGE_DECLARE(toolbox_boot_logo);
}

namespace toolbox::ui {
bool set_text(lv_obj_t* label, const char* value)
{
    if (std::strcmp(lv_label_get_text(label), value) == 0) return false;
    lv_label_set_text(label, value);
    return true;
}
namespace {
// Antialiased alpha mask of the charging bolt at its native 16×20 size.
constexpr uint8_t charge_bolt_pixels[] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  18,   1,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,  33, 232,  49,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   0,  14, 210, 250,   9,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0,   5, 187, 255, 205,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0,   0, 152, 255, 255, 153,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,   0, 123, 255, 255, 255, 100,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,  85, 253, 255, 255, 255,  47,   0,   0,   0,   0, 0,
    0,   0,   0,   0,  58, 247, 255, 255, 255, 255, 124, 119, 119, 119,  96, 0,
    0,   0,   0,  35, 234, 255, 255, 255, 255, 255, 255, 255, 255, 255, 164, 0,
    0,   0,  18, 216, 255, 255, 255, 255, 255, 255, 255, 255, 255, 202,  10, 0,
    0,   5, 188, 255, 255, 255, 255, 255, 255, 255, 255, 255, 222,  22,   0, 0,
    0, 152, 255, 255, 255, 255, 255, 255, 255, 255, 255, 237,  39,   0,   0, 0,
    0,  86, 119, 119, 119, 121, 253, 255, 255, 255, 249,  63,   0,   0,   0, 0,
    0,   0,   0,   0,   0,  41, 255, 255, 255, 254,  94,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0,  95, 255, 255, 255, 127,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0, 148, 255, 255, 167,   1,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   0, 202, 255, 194,   7,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   6, 248, 220,  20,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,  45, 234,  35,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0,   0,   1,  17,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
};
constexpr auto charge_bolt = [] {
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_A8;
    image.header.w = 16;
    image.header.h = 20;
    image.header.stride = image.header.w;
    image.data_size = sizeof(charge_bolt_pixels);
    image.data = charge_bolt_pixels;
    return image;
}();
static_assert(sizeof(charge_bolt_pixels) == charge_bolt.header.stride * charge_bolt.header.h);

void center_cap_height(lv_obj_t* label, const lv_font_t* font)
{
    lv_font_glyph_dsc_t glyph = {};
    if (lv_font_get_glyph_dsc(font, &glyph, '0', 0)) {
        const int center = font->line_height - font->base_line - glyph.ofs_y - glyph.box_h / 2;
        lv_obj_set_style_translate_y(label, font->line_height / 2 - center, 0);
    }
}

struct CachedRow {
    lv_obj_t* object = nullptr;
    lv_draw_buf_t* image = nullptr;
    lv_event_dsc_t* draw = nullptr;
    lv_event_dsc_t* deletion = nullptr;
    std::vector<lv_area_t> ink;

    void release(bool object_deleting = false)
    {
        if (!image) return;
        if (!object_deleting) {
            lv_obj_remove_event_dsc(object, draw);
            lv_obj_remove_event_dsc(object, deletion);
        }
        lv_image_cache_drop(image);
        lv_draw_buf_destroy(image);
        *this = {};
    }
};

constexpr size_t cached_rows = 10;
constexpr size_t max_row_bytes = 512 * 1024;

struct ScrollRows {
    lv_obj_t* viewport;
    lv_timer_t* timer = nullptr;
    bool hidden = false;
    std::array<CachedRow, cached_rows> rows{};
    int scroll_y = 0;
    lv_area_t background_clip{};
    bool background_clipped = false;
};

bool cached_area(const CachedRow& row, lv_area_t& area)
{
    if (!row.object || lv_obj_has_state(row.object, LV_STATE_PRESSED) ||
        lv_obj_has_flag(row.object, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_obj_get_coords(row.object, &area);
    const auto extra = lv_obj_get_ext_draw_size(row.object);
    lv_area_increase(&area, extra, extra);
    return lv_area_get_width(&area) == row.image->header.w &&
           lv_area_get_height(&area) == row.image->header.h;
}

void draw_cached_row(lv_event_t* event)
{
    const auto& row = *static_cast<const CachedRow*>(lv_event_get_user_data(event));
    auto* layer = lv_event_get_layer(event);
    lv_area_t bounds;
    if (layer->color_format != LV_COLOR_FORMAT_RGB565_SWAPPED ||
        layer->opa < LV_OPA_MAX || !cached_area(row, bounds)) return;
    lv_draw_image_dsc_t image;
    lv_draw_image_dsc_init(&image);
    image.src = row.image;
    if (row.image->header.cf == LV_COLOR_FORMAT_A8) {
        image.recolor = lv_obj_get_style_text_color(row.object, LV_PART_MAIN);
        const auto clip = layer->_clip_area;
        for (auto area : row.ink) {
            lv_area_move(&area, bounds.x1, bounds.y1);
            if (lv_area_intersect(&layer->_clip_area, &area, &clip))
                lv_draw_image(layer, &image, &bounds);
        }
    } else {
        lv_draw_image(layer, &image, &bounds);
    }
    // LVGL restores this clip after the row. Its live widgets still handle input.
    layer->_clip_area = {0, 0, -1, -1};
}

void cover_rounded_list(lv_event_t* event)
{
    auto* viewport = lv_event_get_current_target_obj(event);
    auto* cover = static_cast<lv_cover_check_info_t*>(lv_event_get_param(event));
    if (cover->res != LV_COVER_RES_MASKED &&
        lv_obj_get_style_bg_opa(viewport, LV_PART_MAIN) >= LV_OPA_MAX)
        cover->res = LV_COVER_RES_COVER;
}

void draw_list_corners(lv_event_t* event)
{
    auto* viewport = lv_event_get_current_target_obj(event);
    auto* layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(viewport, &bounds);
    const auto radius = std::min(lv_obj_get_style_radius(viewport, LV_PART_MAIN),
                                 std::min(lv_area_get_width(&bounds), lv_area_get_height(&bounds)) / 2);
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_black();
    // The rounded list supplies its own black corner pixels so LVGL
    // can skip drawing the full surface underneath it.
    for (const auto y : {bounds.y1, bounds.y2 - radius + 1}) {
        for (const auto x : {bounds.x1, bounds.x2 - radius + 1}) {
            const lv_area_t corner{x, y, x + radius - 1, y + radius - 1};
            lv_draw_rect(layer, &fill, &corner);
        }
    }
}

void draw_list_background(lv_event_t* event)
{
    auto& cache = *static_cast<ScrollRows*>(lv_event_get_user_data(event));
    auto* layer = lv_event_get_layer(event);
    if (lv_event_get_code(event) == LV_EVENT_DRAW_MAIN_END) {
        if (cache.background_clipped) layer->_clip_area = cache.background_clip;
        cache.background_clipped = false;
        return;
    }
    if (layer->color_format != LV_COLOR_FORMAT_RGB565_SWAPPED || layer->opa < LV_OPA_MAX ||
        lv_obj_get_style_radius(cache.viewport, LV_PART_MAIN) != 0) return;

    const auto clip = layer->_clip_area;
    struct Band { int32_t top, bottom; };
    std::array<Band, cached_rows> bands;
    size_t count = 0;
    for (const auto& row : cache.rows) {
        lv_area_t area;
        if (!cached_area(row, area) || row.image->header.cf != LV_COLOR_FORMAT_RGB565_SWAPPED ||
            lv_obj_get_ext_draw_size(row.object) != 0 ||
            area.x1 > clip.x1 || area.x2 < clip.x2) continue;
        const Band band{std::max(clip.y1, area.y1), std::min(clip.y2, area.y2)};
        if (band.top <= band.bottom) bands[count++] = band;
    }
    if (!count) return;
    std::sort(bands.begin(), bands.begin() + count,
              [](const Band& left, const Band& right) { return left.top < right.top; });
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_black();
    auto top = clip.y1;
    for (size_t i = 0; i < count; ++i) {
        const auto& covered = bands[i];
        if (top < covered.top) {
            const lv_area_t gap{clip.x1, top, clip.x2, covered.top - 1};
            lv_draw_rect(layer, &fill, &gap);
        }
        top = std::max(top, covered.bottom + 1);
    }
    if (top <= clip.y2) {
        const lv_area_t gap{clip.x1, top, clip.x2, clip.y2};
        lv_draw_rect(layer, &fill, &gap);
    }
    // Cached rows already contain their background, including rounded corners.
    cache.background_clip = clip;
    cache.background_clipped = true;
    layer->_clip_area = {0, 0, -1, -1};
}

void find_text_ink(CachedRow& row)
{
    const auto& image = *row.image;
    const int width = image.header.w;
    for (int y = 0; y < image.header.h; ++y) {
        const auto* mask = image.data + y * image.header.stride;
        int first = 0;
        while (first < width && !mask[first]) ++first;
        if (first == width) continue;
        int last = width - 1;
        while (!mask[last]) --last;
        if (!row.ink.empty() && row.ink.back().y2 == y - 1) {
            auto& band = row.ink.back();
            band.x1 = std::min<int32_t>(band.x1, first);
            band.x2 = std::max<int32_t>(band.x2, last);
            band.y2 = y;
        } else {
            row.ink.push_back({first, y, last, y});
        }
    }
}

void invalidate_scrolling_text(lv_event_t* event)
{
    auto& cache = *static_cast<ScrollRows*>(lv_event_get_user_data(event));
    const int y = lv_obj_get_scroll_y(cache.viewport);
    const int delta = y - cache.scroll_y;
    cache.scroll_y = y;
    auto* invalidate = static_cast<bool*>(lv_event_get_param(event));
    const auto& row = cache.rows.front();
    lv_area_t label;
    if (!invalidate || !delta || std::abs(delta) >= lv_obj_get_height(cache.viewport) / 2 ||
        !cached_area(row, label) || row.image->header.cf != LV_COLOR_FORMAT_A8) return;

    // Leave the fixed margins alone, but redraw the moving text in one
    // rectangle to keep LVGL and the panel from processing each line separately.
    lv_area_t viewport;
    lv_obj_get_coords(cache.viewport, &viewport);
    lv_area_t dirty;
    bool has_dirty = false;
    for (auto ink : row.ink) {
        lv_area_move(&ink, label.x1, label.y1);
        ink.y1 += std::min(0, delta);
        ink.y2 += std::max(0, delta);
        if (!lv_area_intersect(&ink, &ink, &viewport)) continue;
        if (has_dirty) lv_area_join(&dirty, &dirty, &ink);
        else dirty = ink;
        has_dirty = true;
    }
    if (has_dirty) lv_obj_invalidate_area(cache.viewport, &dirty);
    *invalidate = false;
}

void prepare_scroll_row(lv_timer_t* timer)
{
    auto& cache = *static_cast<ScrollRows*>(lv_timer_get_user_data(timer));
    if (!lv_obj_is_visible(cache.viewport)) {
        cache.hidden = true;
        lv_timer_pause(timer);
        return;
    }
    lv_obj_update_layout(cache.viewport);
    lv_area_t viewport;
    lv_obj_get_coords(cache.viewport, &viewport);
    auto distance = [&](lv_obj_t* row) {
        lv_area_t area;
        lv_obj_get_coords(row, &area);
        return std::abs((area.y1 + area.y2) - (viewport.y1 + viewport.y2));
    };
    auto* slot = &cache.rows.front();
    for (auto& row : cache.rows) {
        if (!row.object) {
            slot = &row;
            break;
        }
        if (distance(row.object) > distance(slot->object)) slot = &row;
    }
    auto best_distance = slot->object ? distance(slot->object) : INT32_MAX;
    lv_obj_t* next = nullptr;
    const auto count = lv_obj_get_child_count(cache.viewport);
    for (uint32_t i = 0; i < count; ++i) {
        auto* row = lv_obj_get_child(cache.viewport, i);
        const auto rank = distance(row);
        if (rank >= best_distance || lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN) ||
            lv_obj_has_state(row, LV_STATE_PRESSED)) continue;
        if (std::any_of(cache.rows.begin(), cache.rows.end(),
                        [row](const CachedRow& entry) { return entry.object == row; })) continue;
        best_distance = rank;
        next = row;
    }
    if (!next) {
        lv_timer_pause(timer);
        return;
    }
    const bool text_only = lv_obj_check_type(next, &lv_label_class);
    const size_t bytes = static_cast<size_t>(lv_obj_get_width(next)) * lv_obj_get_height(next) * (text_only ? 1 : 2);
    if (bytes > max_row_bytes) {
        lv_timer_pause(timer);
        return;
    }
    auto* pixels = lv_snapshot_take(next, text_only ? LV_COLOR_FORMAT_A8 : LV_COLOR_FORMAT_RGB565_SWAPPED);
    if (!pixels) {
        lv_timer_pause(timer);
        return;
    }
    slot->release();
    slot->object = next;
    slot->image = pixels;
    if (text_only) find_text_ink(*slot);
    slot->draw = lv_obj_add_event_cb(next, draw_cached_row, LV_EVENT_DRAW_MAIN_BEGIN, slot);
    slot->deletion = lv_obj_add_event_cb(next, [](lv_event_t* event) {
        static_cast<CachedRow*>(lv_event_get_user_data(event))->release(true);
    }, LV_EVENT_DELETE, slot);
}
}
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color, int radius)
{
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return object;
}

lv_obj_t* round_page(lv_obj_t* parent)
{
    auto* page = panel(parent, 0, 0, screen_diameter, screen_diameter);
    lv_obj_center(page); // The CO5300 transfer width includes two columns outside the visible circle.
    return page;
}

lv_obj_t* scroll_panel(lv_obj_t* parent, int x, int y, int width, int height, uint32_t color, int radius)
{
    auto* list = panel(parent, x, y, width, height, color, radius);
    lv_obj_add_flag(list, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    return list;
}

lv_obj_t* menu_list(lv_obj_t* parent, int y, int height)
{
    auto* list = scroll_panel(parent, menu_x, y, menu_width, height);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

MenuRow menu_row(lv_obj_t* parent, const char* caption, int x, int y, const char* value,
                 const lv_image_dsc_t* leading, bool chevron)
{
    constexpr int inset = 19, gap = 14;
    auto* object = panel(parent, x, y, menu_width, menu_height, menu_color, menu_radius);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x344861), LV_STATE_PRESSED);
    lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);

    const int left = inset + (leading ? leading->header.w + gap : 0);
    const int right = inset + (chevron ? toolbox_icon_right_28.header.w + gap : 0);
    const int value_width = value ? (chevron ? 110 : 138) : 0;
    auto make_label = [&](const char* content, int width, lv_align_t align, int offset, uint32_t color) {
        auto* label = text(object, content, 0, 0, width, &toolbox_misans_23, color);
        lv_obj_set_height(label, toolbox_misans_23.line_height);
        lv_obj_set_style_text_align(label, align == LV_ALIGN_LEFT_MID ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(label, align, offset, 0);
        center_cap_height(label, &toolbox_misans_23);
        return label;
    };
    make_label(caption, menu_width - left - right - (value ? value_width + 10 : 0),
               LV_ALIGN_LEFT_MID, left, 0xf3f6fc);
    if (leading) icon(object, leading, inset, (menu_height - leading->header.h) / 2);
    if (chevron) icon(object, &toolbox_icon_right_28, menu_width - inset - toolbox_icon_right_28.header.w,
                      (menu_height - toolbox_icon_right_28.header.h) / 2, accent);
    return {object, value ? make_label(value, value_width, LV_ALIGN_RIGHT_MID, -right, accent) : nullptr};
}

// Rasterize immutable rows on a black viewport, or mask plain text labels.
// LV_EVENT_REFRESH discards snapshots when existing row content changes.
void cache_scroll_rows(lv_obj_t* viewport)
{
    auto* cache = new (std::nothrow) ScrollRows{viewport};
    if (!cache) return;
    cache->timer = lv_timer_create(prepare_scroll_row, 36, cache);
    if (!cache->timer) {
        delete cache;
        return;
    }
    auto resume = [](lv_event_t* event) {
        auto& cache = *static_cast<ScrollRows*>(lv_event_get_user_data(event));
        lv_timer_resume(cache.timer);
    };
    lv_obj_add_event_cb(viewport, resume, LV_EVENT_SCROLL, cache);
    lv_obj_add_event_cb(viewport, resume, LV_EVENT_CHILD_CHANGED, cache);
    lv_obj_add_event_cb(viewport, [](lv_event_t* event) {
        auto& cache = *static_cast<ScrollRows*>(lv_event_get_user_data(event));
        if (cache.hidden) {
            cache.hidden = false;
            lv_timer_resume(cache.timer);
        }
    }, LV_EVENT_DRAW_MAIN, cache);
    if (lv_obj_get_style_radius(viewport, LV_PART_MAIN) > 0) {
        lv_obj_add_event_cb(viewport, cover_rounded_list, LV_EVENT_COVER_CHECK, nullptr);
        lv_obj_add_event_cb(viewport, draw_list_corners, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
        if (lv_obj_get_child_count(viewport) == 1 &&
            lv_obj_check_type(lv_obj_get_child(viewport, 0), &lv_label_class)) {
            cache->scroll_y = lv_obj_get_scroll_y(viewport);
            lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_OFF);
            lv_obj_add_event_cb(viewport, invalidate_scrolling_text, LV_EVENT_SCROLL, cache);
        }
    } else {
        lv_obj_add_event_cb(viewport, draw_list_background, LV_EVENT_DRAW_MAIN_BEGIN, cache);
    }
    lv_obj_add_event_cb(viewport, draw_list_background, LV_EVENT_DRAW_MAIN_END, cache);
    lv_obj_add_event_cb(viewport, [](lv_event_t* event) {
        auto& cache = *static_cast<ScrollRows*>(lv_event_get_user_data(event));
        for (auto& row : cache.rows) row.release();
        lv_timer_resume(cache.timer);
    }, LV_EVENT_REFRESH, cache);
    lv_obj_add_event_cb(viewport, [](lv_event_t* event) {
        auto* cache = static_cast<ScrollRows*>(lv_event_get_user_data(event));
        lv_timer_delete(cache->timer);
        for (auto& row : cache->rows) row.release();
        delete cache;
    }, LV_EVENT_DELETE, cache);
}

lv_obj_t* text(lv_obj_t* parent, const char* value, int x, int y, int width,
               const lv_font_t* font, uint32_t color)
{
    auto* object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(object, width);
    lv_obj_set_pos(object, x, y);
    lv_label_set_text(object, value);
    return object;
}

lv_obj_t* icon(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y, uint32_t color)
{
    auto* object = lv_image_create(parent);
    lv_image_set_src(object, source);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_image_recolor(object, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    return object;
}

lv_obj_t* button(lv_obj_t* parent, const char* value, int x, int y, int width, int height,
                 lv_event_cb_t callback, uint32_t color, const lv_font_t* font)
{
    auto* object = panel(parent, x, y, width, height, color, 24);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    auto* caption = text(object, value, 0, 0, width, font, color == accent ? 0x081627 : 0xf3f6fc);
    lv_obj_center(caption);
    center_cap_height(caption, font);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x344861), LV_STATE_PRESSED);
    return object;
}

lv_obj_t* icon_button(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y, int width,
                      int height, lv_event_cb_t callback, uint32_t color)
{
    auto* object = button(parent, "", x, y, width, height, callback, color);
    icon(object, source, (width - source->header.w) / 2, (height - source->header.h) / 2,
         color == accent ? 0x142b47 : 0xa8caff);
    return object;
}

void heading(lv_obj_t* parent, const char* value, lv_event_cb_t back)
{
    text(parent, value, 110, 47, 246, &toolbox_misans_26);
    icon_button(parent, &toolbox_icon_back_28, 63, 82, 46, 46, back);
}

lv_obj_t* boot(lv_obj_t* parent)
{
    auto* root = round_page(parent);
    constexpr uint32_t ink = 0xf3f7fc;
    icon(root, &toolbox_boot_logo, 146, 104, ink);
    const auto& font = toolbox_misans_32;
    text(root, "ToolBox OS", 0, 349 - font.line_height + font.base_line, 466, &font, ink);
    return root;
}

lv_obj_t* row(lv_obj_t* parent, int x, int y, int width, int height, int gap)
{
    auto* object = panel(parent, x, y, width, height);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(object, gap, 0);
    return object;
}

lv_obj_t* row_text(lv_obj_t* parent, const char* value, const lv_font_t* font, uint32_t color)
{
    auto* object = text(parent, value, 0, 0, LV_SIZE_CONTENT, font, color);
    center_cap_height(object, font);
    return object;
}

lv_obj_t* battery(lv_obj_t* parent, uint32_t color, uint32_t charge_color)
{
    // Keep the bolt's slot even when it is hidden so the row never reflows.
    constexpr int bolt_width = charge_bolt.header.w;
    auto* object = panel(parent, 0, 0, 28 + bolt_width, 28);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    auto* outline = panel(object, bolt_width + 2, 7, 20, 14, 0, 2);
    lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outline, 2, 0);
    panel(object, bolt_width + 23, 11, 2, 6, color, 1);
    panel(object, bolt_width + 5, 10, 14, 8, color, 1);
    auto* bolt = icon(object, &charge_bolt, 0, (28 - charge_bolt.header.h) / 2, charge_color);
    lv_obj_add_flag(bolt, LV_OBJ_FLAG_HIDDEN);
    battery_level(object, 100, color);
    return object;
}

bool battery_external_power(lv_obj_t* object, bool connected)
{
    auto* bolt = lv_obj_get_child(object, 3);
    if (lv_obj_has_flag(bolt, LV_OBJ_FLAG_HIDDEN) != connected) return false;
    lv_obj_set_flag(bolt, LV_OBJ_FLAG_HIDDEN, !connected);
    return true;
}

void battery_level(lv_obj_t* object, int percent, uint32_t color)
{
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    auto* body = lv_obj_get_child(object, 0);
    auto* cap = lv_obj_get_child(object, 1);
    auto* fill = lv_obj_get_child(object, 2);
    const auto shade = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_border_color(body, LV_PART_MAIN), shade))
        lv_obj_set_style_border_color(body, shade, 0);
    if (!lv_color_eq(lv_obj_get_style_bg_color(cap, LV_PART_MAIN), shade))
        lv_obj_set_style_bg_color(cap, shade, 0);
    if (lv_obj_get_style_width(fill, LV_PART_MAIN) != 14 * percent / 100)
        lv_obj_set_width(fill, 14 * percent / 100);
    lv_obj_set_flag(fill, LV_OBJ_FLAG_HIDDEN, percent == 0);
    if (!lv_color_eq(lv_obj_get_style_bg_color(fill, LV_PART_MAIN), shade))
        lv_obj_set_style_bg_color(fill, shade, 0);
}
}
