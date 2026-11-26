#include "connection_ui.h"
#include <system/ui.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace toolbox {
namespace {
using sony::MediaState;
constexpr uint32_t card = 0x181d25, control = 0x202631, muted = 0x98a4b7;
constexpr uint32_t blue = 0x9dc6ff, red = 0xfa6268, text = 0xf3f6fc;
constexpr int reviewWidth = 352, reviewHeight = 294, reviewStep = reviewWidth + 12;
constexpr int galleryWidth = 326, galleryHeight = 220, galleryStep = galleryHeight + 14;

void grid(lv_obj_t* parent, int x, int y) {
    for (int i = 0; i < 4; ++i)
        ui::panel(parent, x + i % 2 * 10, y + i / 2 * 10, 8, 8, text, 2);
}
void play(lv_obj_t* parent, int x, int y) {
    auto* icon = ui::panel(parent, x, y, 30, 30, 0, 7);
    lv_obj_set_style_bg_opa(icon, LV_OPA_70, 0);
    lv_obj_add_event_cb(icon, [](lv_event_t* event) {
        lv_area_t area;
        lv_obj_get_coords(lv_event_get_current_target_obj(event), &area);
        lv_draw_triangle_dsc_t triangle;
        lv_draw_triangle_dsc_init(&triangle);
        triangle.color = lv_color_hex(0xa0a0a0);
        triangle.p[0] = {area.x1 + 10, area.y1 + 7};
        triangle.p[1] = {area.x1 + 10, area.y1 + 23};
        triangle.p[2] = {area.x1 + 23, area.y1 + 15};
        lv_draw_triangle(lv_event_get_layer(event), &triangle);
    }, LV_EVENT_DRAW_MAIN_END, nullptr);
}
void trash(lv_obj_t* parent, int x, int y) {
    ui::panel(parent, x, y + 4, 22, 2, red, 1);
    ui::panel(parent, x + 7, y, 8, 2, red, 1);
    auto* bin = ui::panel(parent, x + 3, y + 8, 16, 17, 0, 2);
    lv_obj_set_style_border_width(bin, 2, 0);
    lv_obj_set_style_border_color(bin, lv_color_hex(red), 0);
}
bool pendingDelete(const MediaState& state) { return state.pendingDeletes.front().slot != 0; }
}

bool CameraConnectPage::mediaPage(Page page) {
    return page == Page::Media || page == Page::Gallery || page == Page::DeleteMedia;
}
const sony::MediaItem* CameraConnectPage::selectedMedia() const {
    return mediaSelection_ < mediaOrder_.size() ? &mediaItem(mediaSelection_) : nullptr;
}
const sony::MediaItem& CameraConnectPage::mediaItem(size_t index) const {
    return (*state_.media.items)[mediaOrder_[index]];
}
void CameraConnectPage::indexMedia() {
    mediaOrder_.clear();
    const auto& state = state_.media;
    if (!state.items) return;
    mediaOrder_.reserve(state.items->size());
    for (size_t i = 0; i < state.items->size(); ++i) {
        const auto key = (*state.items)[i].key;
        if (std::find(state.pendingDeletes.begin(), state.pendingDeletes.end(), key) == state.pendingDeletes.end())
            mediaOrder_.push_back(i);
    }
}
void CameraConnectPage::drawMediaTile(int x, int y, int w, int h) {
    auto* box = button("相册", x, y, w, h, LatestMedia, card, &toolbox_misans_20);
    lv_obj_align(lv_obj_get_child(box, 0), LV_ALIGN_CENTER, 0, 17);
    playbackGlyph(box, (w - 26) / 2, 10);
}
void CameraConnectPage::requestMediaImages() {
    sony::MediaWindow window{};
    if (const auto* item = selectedMedia()) {
        if (page_ == Page::Gallery) {
            const auto start = mediaSelection_ / 4 * 4;
            for (size_t i = 0; i < 4 && start + i < mediaOrder_.size(); ++i) window[i] = mediaItem(start + i).key;
        } else if (page_ == Page::DeleteMedia && deleteTarget_) {
            if (item->sameObject(*deleteTarget_)) window[0] = item->key;
        } else {
            window[0] = item->key;
            if (mediaSelection_ + 1 < mediaOrder_.size()) window[1] = mediaItem(mediaSelection_ + 1).key;
            if (mediaSelection_ > 0) window[2] = mediaItem(mediaSelection_ - 1).key;
        }
    }
    connection_.showMedia(window);
}
void CameraConnectPage::mediaAction(int command) {
    if (command == LatestMedia || command == RefreshMedia) {
        if (command == LatestMedia && page_ != Page::Console && page_ != Page::Photo && page_ != Page::Movie) return;
        if (command == RefreshMedia && page_ != Page::Media) return;
        const auto entered = std::chrono::system_clock::now().time_since_epoch();
        if (!connection_.openMedia(command == RefreshMedia)) return;
        if (command == LatestMedia) {
            mediaReference_ = std::chrono::duration_cast<std::chrono::milliseconds>(entered).count();
            mediaBack_ = page_;
        }
        state_.media = connection_.state().media;
        mediaFollowLatest_ = true;
        indexMedia();
        mediaSelection_ = 0;
        feedback(true);
        transition(Page::Media);
        return;
    }
    if (!mediaPage(page_)) return;
    if ((mediaTracking_ || mediaSettling_) && command != Back) return;
    if (command == Back) {
        feedback(true);
        deleteTarget_.reset();
        transition(page_ == Page::Media ? mediaBack_ : Page::Media);
        return;
    }
    const auto* item = selectedMedia();
    if (!item) return;
    const auto& list = state_.media.items;
    if (command == MediaListPage && page_ == Page::Media) {
        mediaFollowLatest_ = false;
        feedback(true);
        transition(Page::Gallery);
    } else if (command == AskDelete && page_ == Page::Media && state_.mediaDeleteAvailable && !item->protectedItem &&
               state_.media.status != MediaState::ListFailed && !state_.media.pendingDeletes.back().slot) {
        deleteTarget_ = std::shared_ptr<const sony::MediaItem>(list, item);
        mediaFollowLatest_ = false;
        feedback(true);
        transition(Page::DeleteMedia);
    } else if (command == ConfirmDelete && page_ == Page::DeleteMedia) {
        if (!deleteTarget_ || !item->sameObject(*deleteTarget_)) return;
        if (connection_.deleteMedia(deleteTarget_)) {
            feedback(true);
            state_.media = connection_.state().media;
            indexMedia();
            mediaSelection_ = mediaOrder_.empty() ? 0 : std::min(mediaSelection_, mediaOrder_.size() - 1);
            mediaFollowLatest_ = false;
            deleteTarget_.reset();
            transition(Page::Media);
        }
    } else if ((command == MediaLeftOrUp || command == MediaRightOrDown) &&
               (page_ == Page::Media || page_ == Page::Gallery)) {
        const auto next = mediaNeighbor(command == MediaLeftOrUp ? -1 : 1);
        if (next == mediaSelection_) return;
        mediaFollowLatest_ = false;
        mediaSelection_ = next;
        feedback(true);
        drawMediaContent();
    } else if (page_ == Page::Gallery && command >= MediaPickBase && command < MediaPickBase + 4) {
        const auto next = mediaSelection_ / 4 * 4 + command - MediaPickBase;
        if (next >= mediaOrder_.size()) return;
        mediaFollowLatest_ = false;
        mediaSelection_ = next;
        feedback(true);
        transition(Page::Media);
    }
}
void CameraConnectPage::drawMedia() {
    mediaContent_ = ui::panel(root_, 0, 83, 466, 367);
    lv_obj_set_style_bg_opa(mediaContent_, LV_OPA_TRANSP, 0);
    if (page_ != Page::DeleteMedia) trackMediaTouch(mediaContent_);
    title(page_ == Page::DeleteMedia ? "删除此媒体？" : page_ == Page::Gallery ? "相册" : "媒体回看",
          page_ != Page::DeleteMedia);
    drawMediaContent();
}
void CameraConnectPage::drawMediaContent() {
    stopMediaSwipe();
    lv_obj_clean(mediaContent_);
    clearMediaViews();
    mediaViewport_ = nullptr;
    mediaSlides_ = {};
    const auto& state = state_.media;
    const auto* item = page_ == Page::DeleteMedia ? deleteTarget_.get() : selectedMedia();
    const char* result = state.deleteResult == MediaState::Unconfirmed ? "删除结果未确认" :
                         state.deleteResult == MediaState::DeleteFailed ?
                             (state.deleteError == 0x2019 ? "相机忙，删除失败" : "删除失败") :
                         pendingDelete(state) ? "正在删除" :
                         state.deleteResult == MediaState::Deleted ? "已删除" : "";
    auto caption = [&](const char* s, int x, int y, int w, int h, const lv_font_t* font, uint32_t color = text) {
        return label(s, x, y - 83, w, h, font, color, false, mediaContent_);
    };
    auto action = [&](const char* s, int x, int y, int w, int h, int cmd, uint32_t bg, uint32_t color = text) {
        return button(s, x, y - 83, w, h, cmd, bg, &toolbox_misans_20, 18, color, mediaContent_);
    };
    if (!item) {
        const char* notice = state.status == MediaState::Loading ? "正在读取相册" :
                             pendingDelete(state) ? "正在删除" :
                             state.status == MediaState::ListFailed ? "无法读取相册" : "暂无媒体";
        caption(notice, 83, 180, 300, 78, &toolbox_misans_26, muted);
        if (state.status == MediaState::ListFailed || state.status == MediaState::Unconfirmed)
            action("重试", 158, 287, 150, 55, RefreshMedia, control);
        if (*result && !pendingDelete(state)) caption(result, 111, 270, 244, 40, &toolbox_misans_20, muted);
        action("返回", 143, 381, 180, 49, Back, 0, muted);
    } else if (page_ == Page::Media) {
        drawMediaSlides();
        grid(cornerButton(cornerRight, MediaListPage, mediaContent_, 83), (cornerSize - 18) / 2, (cornerSize - 18) / 2);
        const char* note = *result ? result :
                           state.status == MediaState::ListFailed ? "相册同步失败" :
                           state.status == MediaState::Loading ? "正在同步相册" : "";
        if (*note) {
            // Status floats over the preview's lower edge like the V4 notice, keeping the delete row clear.
            auto* pill = lv_obj_get_parent(caption(note, 113, 288, 240, 34, &toolbox_misans_20, text));
            lv_obj_set_style_bg_color(pill, lv_color_hex(0x303a48), 0);
            lv_obj_set_style_bg_opa(pill, LV_OPA_90, 0);
            lv_obj_set_style_radius(pill, 17, 0);
        }
        if ((state.status == MediaState::ListFailed || state.status == MediaState::Unconfirmed) && !pendingDelete(state))
            action("重试", 173, 390, 120, 44, RefreshMedia, control);
        else if (state_.mediaDeleteAvailable && !item->protectedItem && !state.pendingDeletes.back().slot) {
            auto* remove = action("删除", 169, 390, 128, 44, AskDelete, 0x312025, red);
            lv_obj_align(lv_obj_get_child(remove, 0), LV_ALIGN_CENTER, 11, 0);
            trash(remove, 22, 9);
        }
    } else if (page_ == Page::Gallery) {
        drawMediaSlides();
        const auto count = std::to_string(mediaSelection_ / 4 + 1) + " / " + std::to_string((mediaOrder_.size() + 3) / 4);
        caption(count.c_str(), 147, 369, 172, 38, &toolbox_misans_20, muted);
        if (*result) caption(result, 100, 407, 266, 28, &toolbox_misans_20, muted);
    } else {
        const auto* selected = selectedMedia();
        const bool same = selected && selected->sameObject(*item);
        const bool group = item->files.size() > 1;
        const int height = group ? 126 : 155;
        drawMediaImage(0, *item, ui::panel(mediaContent_, 115, 98 - 83, 236, height, card, 0), 236, height);
        const auto& primary = item->files.front();
        caption(primary.name(), 75, group ? 226 : 258, 316, 30, &toolbox_misans_24);
        if (group) caption(item->files[1].name(), 75, 256, 316, 26, &toolbox_misans_20, muted);
        auto scope = "卡槽 " + std::to_string(item->key.slot);
        if (group && item->files.size() == 2) {
            const auto a = primary.type(), b = item->files[1].type();
            scope += " · ";
            scope += a == "JPEG" && b == "RAW" ? "RAW + JPEG" : a + " + " + b;
        } else if (!group) scope += " · " + primary.type();
        if (group) scope += " · 全部 " + std::to_string(item->files.size()) + " 个文件";
        caption(scope.c_str(), 44, 285, 378, 32, &toolbox_misans_20, muted);
        if (!same) caption("媒体已变化，请重新选择", 77, 316, 312, 29, &toolbox_misans_20, muted);
        const auto choice = [&](const char* s, int x, int cmd, uint32_t bg, uint32_t color) {
            button(s, x, confirmTop - 83, confirmWidth, confirmHeight, cmd, bg, &toolbox_misans_26, 24, color, mediaContent_);
        };
        choice("取消", confirmLeft, Back, control, text);
        if (!same || state.status == MediaState::ListFailed || state.pendingDeletes.back().slot)
            choice("删除", confirmRight, None, control, muted);
        else choice("删除", confirmRight, ConfirmDelete, red, 0x2a080d);
    }
    requestMediaImages();
    updateMediaImages();
}
size_t CameraConnectPage::mediaNeighbor(int side) const {
    const auto next = page_ == Page::Gallery ? int(mediaSelection_ / 4 * 4) + side * 4
                                             : int(mediaSelection_) - side;
    return next >= 0 && size_t(next) < mediaOrder_.size() ? size_t(next) : mediaSelection_;
}
void CameraConnectPage::drawMediaSlides() {
    const bool gallery = page_ == Page::Gallery;
    mediaViewport_ = ui::panel(mediaContent_, gallery ? 70 : 57, gallery ? 51 : 7,
                               gallery ? galleryWidth : reviewWidth, gallery ? galleryHeight : reviewHeight);
    lv_obj_set_style_bg_opa(mediaViewport_, LV_OPA_TRANSP, 0);
    trackMediaTouch(mediaViewport_);
    drawMediaSlide(mediaSelection_, 0);
    for (int side : {-1, 1}) {
        const auto neighbor = mediaNeighbor(side);
        if (neighbor != mediaSelection_) drawMediaSlide(neighbor, side);
    }
}
void CameraConnectPage::drawMediaSlide(size_t selection, int side) {
    const bool gallery = page_ == Page::Gallery;
    auto* panel = ui::panel(mediaViewport_, 0, 0, gallery ? galleryWidth : reviewWidth,
                           gallery ? galleryHeight : reviewHeight);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    mediaSlides_[side + 1] = panel;
    const auto firstView = size_t(side + 1) * 4;
    if (gallery) {
        const auto start = selection / 4 * 4;
        for (size_t i = 0; i < 4 && start + i < mediaOrder_.size(); ++i) {
            auto* tile = button("", i % 2 * 168, i / 2 * 116, 158, 104, MediaPickBase + i,
                                card, &toolbox_misans_20, 0, text, panel);
            drawMediaImage(firstView + i, mediaItem(start + i), tile, 158, 104);
            if (!side) trackMediaTouch(tile);
            else lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
            if (start + i == selection) {
                auto* outline = ui::panel(tile, 0, 0, 158, 104, 0, 1);
                lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_color(outline, lv_color_hex(blue), 0);
                lv_obj_set_style_border_width(outline, 2, 0);
            }
        }
    } else {
        const auto& item = mediaItem(selection);
        label(item.files.front().name(), 65, 0, 222, 34, &toolbox_misans_24, text, false, panel);
        drawMediaImage(firstView, item, ui::panel(panel, 0, 46, reviewWidth, 186, card, 0), reviewWidth, 186);
        auto date = sony::mediaDate(item);
        if (item.video() && !date.empty()) date.insert(0, "创建 ");
        label(date.empty() ? "时间不可用" : date.c_str(), 18, 236, 316, 26, &toolbox_misans_20, muted, false, panel);
        label(sony::mediaAge(item, mediaReference_).c_str(), 44, 262, 264, 30,
              &toolbox_misans_26, blue, false, panel);
    }
    if (side) lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}
void CameraConnectPage::drawMediaImage(size_t view, const sony::MediaItem& media, lv_obj_t* box, int w, int h) {
    auto& v = mediaViews_[view];
    v.region = box;
    v.key = media.key;
    v.image = lv_image_create(box);
    lv_obj_center(v.image);
    lv_obj_add_flag(v.image, LV_OBJ_FLAG_HIDDEN);
    v.placeholder = label("正在读取", 0, 0, w, h, &toolbox_misans_20, muted, false, box);
    if (media.video()) play(box, w - 38, h - 37);
}
void CameraConnectPage::clearMediaViews() {
    for (auto& view : mediaViews_) {
        if (view.pixels.pixels) lv_image_cache_drop(&view.descriptor);
        view = {};
    }
}
void CameraConnectPage::updateMediaImages() {
    for (auto& v : mediaViews_) {
        if (!v.region) continue;
        // A confirmation can outlive a changed catalog. Never show a replacement object's image.
        const auto* selected = selectedMedia();
        const bool same = page_ != Page::DeleteMedia || (selected && selected->sameObject(*deleteTarget_));
        auto& tiles = state_.media.tiles;
        auto found = std::find_if(tiles.begin(), tiles.end(), [&](const sony::MediaTile& t) { return t.key == v.key; });
        auto image = !same || found == tiles.end() ? sony::MediaImage{} : found->image;
        if (image.pixels != v.pixels.pixels) {
            if (v.pixels.pixels) lv_image_cache_drop(&v.descriptor);
            lv_obj_add_flag(v.image, LV_OBJ_FLAG_HIDDEN);
            if (image.pixels) {
                v.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
                v.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
                v.descriptor.header.w = image.width;
                v.descriptor.header.h = image.height;
                v.descriptor.header.stride = image.width * 2;
                v.descriptor.data_size = image.width * image.height * 2;
                v.descriptor.data = reinterpret_cast<const uint8_t*>(image.pixels.get());
                lv_image_set_src(v.image, &v.descriptor);
                lv_obj_update_layout(v.region);
                const auto scale = std::min(lv_obj_get_width(v.region) * 256 / image.width,
                                            lv_obj_get_height(v.region) * 256 / image.height);
                lv_image_set_scale(v.image, scale);
                lv_obj_center(v.image);
                lv_obj_remove_flag(v.image, LV_OBJ_FLAG_HIDDEN);
            }
            v.pixels = std::move(image);
        }
        const bool pending = same && found == tiles.end() &&
                             state_.media.status != MediaState::ListFailed && state_.media.status != MediaState::Unconfirmed;
        ui::set_text(v.placeholder, pending ? "正在读取" : "无预览");
        if (v.pixels.pixels) lv_obj_add_flag(v.placeholder, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(v.placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}
void CameraConnectPage::updateMedia(const MediaState& previous) {
    const auto& next = state_.media;
    const auto browseStatus = [](MediaState::Status status) {
        return status == MediaState::Checking || status == MediaState::Deleting ? MediaState::Ready : status;
    };
    const bool reindex = previous.items != next.items || previous.pendingDeletes != next.pendingDeletes;
    if (reindex) {
        pending_ = None;
        const auto* old = previous.items && mediaSelection_ < mediaOrder_.size()
                          ? &(*previous.items)[mediaOrder_[mediaSelection_]] : nullptr;
        indexMedia();
        if (mediaOrder_.empty() || mediaFollowLatest_) mediaSelection_ = 0;
        else {
            const auto match = std::find_if(mediaOrder_.begin(), mediaOrder_.end(), [&](size_t i) {
                return old && (*next.items)[i].key == old->key;
            });
            mediaSelection_ = match == mediaOrder_.end() ? std::min(mediaSelection_, mediaOrder_.size() - 1) :
                                                         size_t(match - mediaOrder_.begin());
        }
        if (next.status != MediaState::Loading) mediaFollowLatest_ = false;
    }
    if (reindex || browseStatus(previous.status) != browseStatus(next.status) || previous.deleteRevision != next.deleteRevision)
        drawMediaContent();
    else updateMediaImages();
}
void CameraConnectPage::trackMediaTouch(lv_obj_t* box) {
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(box, mediaTouch, LV_EVENT_ALL, this);
}
void CameraConnectPage::mediaTouch(lv_event_t* event) {
    auto* self = static_cast<CameraConnectPage*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (!self->mediaViewport_ || self->mediaSettling_ ||
        lv_event_get_target_obj(event) != lv_event_get_current_target_obj(event)) return;
    if (code == LV_EVENT_PRESS_LOST) {
        if (self->mediaTracking_) self->settleMedia(0);
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
    auto* input = lv_indev_active();
    if (!input) return;
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        self->mediaTracking_ = true;
        self->mediaSwiped_ = false;
        self->mediaWrongAxis_ = false;
        self->mediaTouchAt_ = point;
        return;
    }
    if (!self->mediaTracking_) return;
    const int dx = point.x - self->mediaTouchAt_.x, dy = point.y - self->mediaTouchAt_.y;
    const bool gallery = self->page_ == Page::Gallery;
    const int along = gallery ? dy : dx, across = gallery ? dx : dy;
    if (!self->mediaSwiped_ && (std::abs(along) >= 9 || std::abs(across) >= 9)) {
        self->mediaSwiped_ = true;
        self->mediaWrongAxis_ = std::abs(across) >= std::abs(along);
    }
    if (!self->mediaSwiped_ || self->mediaWrongAxis_) {
        if (code == LV_EVENT_RELEASED) self->mediaTracking_ = false;
        return;
    }
    const int side = along > 0 ? -1 : 1;
    const bool canTurn = self->mediaSlides_[side + 1] != nullptr;
    const auto step = gallery ? galleryStep : reviewStep;
    const auto distance = std::clamp(canTurn ? along : along / 4, -step, step);
    self->moveMedia(distance);
    if (code == LV_EVENT_RELEASED) {
        self->settleMedia(canTurn && std::abs(along) >= (gallery ? 49 : 46) ? side : 0);
    }
}
void CameraConnectPage::animateMediaOffset(void* self, int32_t offset) {
    static_cast<CameraConnectPage*>(self)->moveMedia(offset);
}
void CameraConnectPage::moveMedia(int offset) {
    if (offset == mediaOffset_) return;
    mediaOffset_ = offset;
    const bool gallery = page_ == Page::Gallery;
    for (int side : {-1, 0, 1}) {
        auto* panel = mediaSlides_[side + 1];
        if (!panel) continue;
        if (side && side * offset >= 0) {
            lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int position = side * (gallery ? galleryStep : reviewStep) + offset;
        if (gallery) lv_obj_set_style_translate_y(panel, position, 0);
        else lv_obj_set_style_translate_x(panel, position, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}
void CameraConnectPage::settleMedia(int side) {
    mediaTracking_ = false;
    if (!side && !mediaOffset_) return;
    mediaSettling_ = true;
    mediaSettleSide_ = side;
    const auto offset = -side * (page_ == Page::Gallery ? galleryStep : reviewStep);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_user_data(&animation, this);
    lv_anim_set_values(&animation, mediaOffset_, offset);
    lv_anim_set_duration(&animation, side ? 175 : 135);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, animateMediaOffset);
    lv_anim_set_completed_cb(&animation, [](lv_anim_t* a) {
        auto* self = static_cast<CameraConnectPage*>(lv_anim_get_user_data(a));
        const auto side = self->mediaSettleSide_;
        self->mediaSettleSide_ = 0;
        self->mediaSettling_ = false;
        if (side) self->pending_ = side < 0 ? MediaLeftOrUp : MediaRightOrDown;
    });
    lv_anim_start(&animation);
}
void CameraConnectPage::stopMediaSwipe() {
    lv_anim_delete(this, animateMediaOffset);
    mediaOffset_ = mediaSettleSide_ = 0;
    mediaTracking_ = mediaSwiped_ = mediaWrongAxis_ = mediaSettling_ = false;
}
}
