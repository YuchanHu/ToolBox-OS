#include "connection_ui.h"
#include <hal/hal.h>
#include <system/touch_input.h>
#include <system/shell.h>
#include <system/ui.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace toolbox {
namespace {
using sony::CaptureAction;
using sony::Parameter;
using sony::Recording;
constexpr uint32_t card = 0x181d25, control = 0x202631, muted = 0x98a4b7;
constexpr uint32_t blue = 0x9dc6ff, ink = 0x081627, green = 0x62d9a1, red = 0xfa6268;
constexpr int wheelHeight = 196, wheelStep = 66;
void ring(lv_obj_t* root, uint32_t color, int width) {
    auto* circle = ui::panel(root, 8, 8, 450, 450, 0, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(circle, width, 0);
}
std::string join(std::string a, std::string b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " / " + b;
}
}

std::string CameraConnectPage::captureValue(Parameter id) const {
    auto* p = state_.capture.find(id);
    return p ? sony::parameterText(id, p->current) : "";
}
void CameraConnectPage::drawCameraStatus(int y) {
    auto* row = ui::row(root_, 0, y, 466, 32, 8);
    clock_ = ui::row_text(row, "", &toolbox_misans_20, 0x9daebd);
    watchBattery_ = ui::battery(row, 0xbdcbd8);
    watchPercent_ = ui::row_text(row, "", &toolbox_misans_20, 0xcbd6e0);
    clockReadAt_ = GetHAL().millis() - 1000;
    updateClock(GetHAL().millis());
    const auto* p = state_.capture.find(Parameter::Battery);
    if (p && p->current >= 0 && p->current <= 100) {
        auto* battery = ui::battery(row, 0xbdcbd8);
        ui::battery_level(battery, int(p->current), 0xbdcbd8);
        const auto percentage = "相机 " + std::to_string(p->current) + "%";
        ui::row_text(row, percentage.c_str(), &toolbox_misans_20, 0xcbd6e0);
    }
}
void CameraConnectPage::updateClock(uint32_t now) {
    if (!clock_ || now - clockReadAt_ < 1000) return;
    clockReadAt_ = now;
    const auto wall = std::time(nullptr);
    std::tm local{};
    localtime_r(&wall, &local);
    ui::set_text_fmt(clock_, "%02d:%02d", local.tm_hour, local.tm_min);
    const auto power = GetHAL().getBatteryStatus();
    ui::battery_level(watchBattery_, power.percent, 0xbdcbd8);
    ui::battery_external_power(watchBattery_, power.external_power);
    ui::set_text_fmt(watchPercent_, "本机 %u%%", unsigned(power.percent));
}
void CameraConnectPage::captureTile(const char* name, const lv_image_dsc_t* glyph, int x, int y, int w, int h, int cmd) {
    auto* box = button(name, x, y, w, h, cmd, card, &toolbox_misans_20);
    lv_obj_align(lv_obj_get_child(box, 0), LV_ALIGN_CENTER, 0, 17);
    ui::icon(box, glyph, (w - glyph->header.w) / 2, 9);
}
void CameraConnectPage::drawCapture() {
    const bool movie = page_ == Page::Movie;
    const auto& capture = state_.capture;
    const bool ready = capture.recording == Recording::Idle;
    ring(root_, ready ? (movie ? green : blue) : muted, 3);
    title(movie ? "录像" : "拍照");
    auto mode = captureValue(Parameter::Mode);
    if (!mode.empty()) mode = "机身 " + mode;
    auto status = ready ? (movie ? join("就绪", mode) : mode) : "录像状态未知";
    if (!status.empty()) label(status.c_str(), 89, 88, 288, 32, &toolbox_misans_20, ready ? green : muted);
    const Parameter ids[] = {Parameter::Shutter, Parameter::Aperture, Parameter::Iso};
    for (int i = 0; i < 3; ++i) {
        auto* property = capture.find(ids[i]);
        auto* box = property && property->options() && !capture.locked()
            ? button("", 70 + i * 109, 135, 106, 84, EditBase + uint16_t(ids[i]), control, &toolbox_misans_20)
            : ui::panel(root_, 70 + i * 109, 135, 106, 84, control, 24);
        label(sony::parameterName(ids[i]), 0, 13, 106, 25, &toolbox_misans_20, muted, false, box);
        auto value = captureValue(ids[i]);
        label(value.empty() ? "-" : value.c_str(), 3, 37, 100, 35, &toolbox_misans_26, 0xf3f6fc, false, box);
    }
    auto detail = movie ? join(captureValue(Parameter::MovieFormat), captureValue(Parameter::MovieRate))
                        : join(captureValue(Parameter::Focus), captureValue(Parameter::PhotoFormat));
    label(detail.c_str(), 75, 230, 316, 34, &toolbox_misans_20, muted);
    drawCameraStatus(269);
    const bool album = ready && state_.mediaAvailable;
    if (capture.parameters && !capture.parameters->empty())
        captureTile("参数", &toolbox_icon_settings_28, album ? 85 : 114, 315, album ? 90 : 106, 72, ParametersPage);
    if (album) drawMediaTile(188, 315, 90, 72);
    if (movie ? capture.canPhoto : capture.canMovie)
        captureTile(movie ? "拍照" : "录像", movie ? &toolbox_camera_photo_small : &toolbox_camera_movie,
                    album ? 291 : 246, 315, album ? 90 : 106, 72, movie ? ShootPage : MoviePage);
    if (!captureNotice_.empty()) label(captureNotice_.c_str(), 95, 391, 276, 26, &toolbox_misans_20, muted);
    const char* hint = capture.pending == CaptureAction::Photo ? "正在请求快门" :
                       !ready ? "" : movie ? "KEYB 开始录像" : "KEYB 拍照";
    label(hint, 95, captureNotice_.empty() ? 396 : 421, 276, 31, &toolbox_misans_20, muted);
}
void CameraConnectPage::drawParameters() {
    const bool movie = parameterBack_ == Page::Movie;
    title(movie ? "录像参数" : "拍照参数", true);
    if (!captureNotice_.empty()) label(captureNotice_.c_str(), 111, 88, 244, 30, &toolbox_misans_20, muted);
    parameterList_ = ui::menu_list(root_, 138, 232);
    lv_obj_set_scrollbar_mode(parameterList_, LV_SCROLLBAR_MODE_AUTO);
    ui::cache_scroll_rows(parameterList_);
    int y = 0;
    for (size_t i = 0; i < sony::parameterSpecCount; ++i) {
        const auto& spec = sony::parameterSpecs[i];
        if (!(spec.pages & (movie ? sony::moviePage : sony::photoPage))) continue;
        auto* p = state_.capture.find(spec.id);
        if (!p) continue;
        const bool edit = p->options() != 0;
        auto* row = menu(parameterList_, spec.name, y, edit ? EditBase + uint16_t(spec.id) : None,
                         sony::parameterText(spec.id, p->current).c_str());
        if (!edit) lv_obj_set_style_text_color(lv_obj_get_child(row, -1), lv_color_hex(muted), 0);
        y += ui::menu_pitch;
    }
    if (!y) label("暂无可读参数", 78, 176, 310, 66, &toolbox_misans_26, muted);
    lv_obj_update_layout(parameterList_);
    if (parameterScroll_) lv_obj_scroll_to_y(parameterList_, parameterScroll_, LV_ANIM_OFF);
    bottom("返回", Back);
}
void CameraConnectPage::wheelTouch(lv_event_t* event) {
    auto& page = *static_cast<CameraConnectPage*>(lv_event_get_user_data(event));
    auto* input = lv_indev_active();
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (page.state_.capture.pending == CaptureAction::Set) return;
    const int delta = point.y - page.wheelY_;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        page.stopWheelAnimation();
        page.wheelDragging_ = true;
        page.wheelMoved_ = false;
        page.showWheelValues(page.selection_);
        page.wheelY_ = point.y;
        break;
    case LV_EVENT_PRESSING:
        page.dragWheel(delta);
        break;
    case LV_EVENT_RELEASED: {
        page.dragWheel(delta);
        page.wheelDragging_ = false;
        if (page.wheelMoved_) {
            page.pending_ = WheelReleased;
        } else {
            lv_area_t area;
            lv_obj_get_coords(lv_event_get_current_target_obj(event), &area);
            const int offset = point.y - area.y1 - wheelHeight / 2;
            if (std::abs(delta) < 8 && std::abs(offset) > 38)
                page.pending_ = offset < 0 ? PreviousValue : NextValue;
            else page.settleWheel(0);
        }
        break;
    }
    case LV_EVENT_PRESS_LOST:
        page.wheelDragging_ = false;
        page.showWheelValues(page.selection_);
        break;
    default: break;
    }
}
void CameraConnectPage::dragWheel(int delta) {
    const auto* property = state_.capture.find(editing_);
    if (!wheelDragging_ || !property || !property->options()) return;
    if (std::abs(delta) > 8) wheelMoved_ = true;
    const auto count = int64_t(property->options());
    // Index -1 represents the camera's current value when it is outside today's allowed set.
    const int64_t origin = selection_ < property->options() ? int64_t(selection_) : -1;
    const int64_t minIndex = origin < 0 ? -1 : 0;
    const auto position = std::clamp(origin * wheelStep - delta * 2,
                                     minIndex * wheelStep - 12, (count - 1) * wheelStep + 12);
    const auto index = origin + (position - origin * wheelStep) / wheelStep;
    const auto center = index < 0 ? property->options() : size_t(index);
    if (center != wheelCenter_) showWheelValues(center);
    moveWheel(int(index * wheelStep - position));
}
void CameraConnectPage::finishWheelDrag() {
    const auto* property = state_.capture.find(editing_);
    if (!property || !property->options()) return;
    const auto count = property->options();
    auto next = wheelCenter_;
    int offset = 0;
    if (wheelOffset_ < -wheelStep / 2 && (next == count || next + 1 < count)) {
        next = next == count ? 0 : next + 1;
        offset = -wheelStep;
    } else if (wheelOffset_ > wheelStep / 2 && next > 0 && next < count) {
        --next;
        offset = wheelStep;
    }
    if (next != selection_) {
        selection_ = next;
        feedback(true);
    }
    settleWheel(offset);
}
void CameraConnectPage::moveWheel(int offset) {
    wheelOffset_ = offset;
    lv_obj_set_y(wheelTrack_, offset);
}
void CameraConnectPage::stopWheelAnimation() { lv_anim_delete(this, nullptr); }
void CameraConnectPage::settleWheel(int offset) {
    stopWheelAnimation();
    if (offset == wheelOffset_) { showWheelValues(selection_); return; }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_user_data(&anim, this);
    lv_anim_set_values(&anim, wheelOffset_, offset);
    lv_anim_set_duration(&anim, 155);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void* self, int32_t y) { static_cast<CameraConnectPage*>(self)->moveWheel(y); });
    lv_anim_set_completed_cb(&anim, [](lv_anim_t* a) {
        auto* page = static_cast<CameraConnectPage*>(lv_anim_get_user_data(a));
        page->showWheelValues(page->selection_);
    });
    lv_anim_start(&anim);
}
void CameraConnectPage::showWheelValues(size_t center) {
    const auto* p = state_.capture.find(editing_);
    if (!p || !wheelTrack_) return;
    wheelCenter_ = center;
    const bool centerAllowed = center < p->options();
    moveWheel(0);
    for (size_t i = 0; i < wheelValues_.size(); ++i) {
        auto* slot = wheelValues_[i];
        const int offset = int(i) - 2;
        const auto index = centerAllowed ? int64_t(center) + offset : offset - 1;
        const bool available = index >= 0 && uint64_t(index) < p->options();
        lv_obj_set_flag(slot, LV_OBJ_FLAG_HIDDEN, !available && offset != 0);
        if (!available && offset != 0) continue;
        const auto value = sony::parameterText(editing_, available ? p->option(size_t(index)) : p->current);
        const bool shortAscii = value.size() <= 16 &&
            std::all_of(value.begin(), value.end(), [](unsigned char c) { return c < 128; });
        const auto* font = offset == 0 && shortAscii ? &toolbox_misans_36 : &toolbox_misans_26;
        lv_obj_set_style_text_font(slot, font, 0);
        lv_obj_set_style_text_color(slot, lv_color_hex(offset == 0 ? blue : muted), 0);
        lv_obj_set_y(slot, wheelHeight / 2 + offset * wheelStep - font->line_height / 2);
        ui::set_text(slot, value.c_str());
    }
    const bool selectable = selection_ < p->options();
    const bool pending = state_.capture.pending == CaptureAction::Set;
    lv_obj_set_flag(applyButton_, LV_OBJ_FLAG_HIDDEN, pending || !selectable);
    lv_obj_set_flag(applyHint_, LV_OBJ_FLAG_HIDDEN, !pending && selectable);
    ui::set_text(applyHint_, pending ? "正在读取相机结果" : "滑动选择");
}
void CameraConnectPage::drawValue() {
    const auto* p = state_.capture.find(editing_);
    if (!p || !p->options()) return;
    title(sony::parameterName(editing_), true);
    auto current = "相机当前 " + sony::parameterText(editing_, p->current);
    label(current.c_str(), 71, 110, 324, 43, &toolbox_misans_20, muted);
    ui::panel(root_, 72, 217, 322, 76, card, 23);
    auto* wheel = ui::panel(root_, 72, 157, 322, wheelHeight);
    lv_obj_set_style_bg_opa(wheel, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wheel, lv_obj_flag_t(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    for (auto code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(wheel, wheelTouch, code, this);
    wheelTrack_ = ui::panel(wheel, 0, 0, 322, wheelHeight);
    lv_obj_set_style_bg_opa(wheelTrack_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wheelTrack_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    for (auto*& slot : wheelValues_) {
        slot = ui::text(wheelTrack_, "", 5, 0, 312, &toolbox_misans_26, muted);
        lv_label_set_long_mode(slot, LV_LABEL_LONG_MODE_DOTS);
    }
    applyHint_ = label("", 124, 357, 218, 48, &toolbox_misans_20, muted);
    applyButton_ = button("应用", 138, 357, 190, 48, ApplyValue, blue, &toolbox_misans_26, 23, ink);
    showWheelValues(selection_);
    if (!captureNotice_.empty()) label(captureNotice_.c_str(), 102, 410, 262, 26, &toolbox_misans_20, muted);
}
void CameraConnectPage::drawRecording() {
    const auto& capture = state_.capture;
    const bool active = capture.recording == Recording::Active;
    ring(root_, active ? red : muted, active ? 10 : 3);
    if (active) {
        ui::panel(root_, 183, 77, 12, 12, red, 6);
        label("REC", 198, 60, 84, 46, &toolbox_misans_32, red);
    } else {
        label(capture.pending == CaptureAction::Start ? "正在确认录像" : "录像状态未知",
              87, 62, 292, 48, &toolbox_misans_26, muted);
    }
    timer_ = label("--:--:--", 52, 155, 362, 98, &toolbox_misans_65, active ? 0xf3f6fc : muted);
    if (active) {
        label(capture.localStart ? "本机估算" : "从检测到录像时计时", 92, 247, 282, 31, &toolbox_misans_20, muted);
        auto detail = join(captureValue(Parameter::MovieFormat), captureValue(Parameter::MovieRate));
        label(detail.c_str(), 88, 285, 290, 33, &toolbox_misans_20, muted);
        drawCameraStatus(319);
    }
    if (active && !captureNotice_.empty())
        label(captureNotice_.c_str(), 96, 349, 274, 28, &toolbox_misans_20, muted);
    const char* hint = capture.pending == CaptureAction::Stop ? "正在确认停止" :
                       active && capture.canMovie ? "KEYB 停止录像" :
                       state_.phase == sony::ConnectionPhase::Failed ? "KEYB 重试连接" :
                       state_.phase == sony::ConnectionPhase::Connecting ? "正在重连相机" : captureNotice_.c_str();
    label(hint, 102, 374, 262, 38, &toolbox_misans_20, muted);
}
void CameraConnectPage::updateRecording(uint32_t now) {
    if (!timer_ || state_.capture.recording != Recording::Active) return;
    const auto elapsed = state_.capture.elapsed(now);
    if (elapsed == shownSeconds_) return;
    shownSeconds_ = elapsed;
    ui::set_text_fmt(timer_, "%02u:%02u:%02u", unsigned(elapsed / 3600), unsigned(elapsed / 60 % 60), unsigned(elapsed % 60));
}

void CameraConnectPage::lockRecording(bool locked) {
    if (locked == mute_) return;
    mute_ = locked;
    GetHAL().setCameraRecordingMute(locked);
    set_touch_input_blocked(GetHAL().lvTouchpad, locked);
    if (locked) {
        pending_ = None;
        shell_note_activity();
    }
}
void CameraConnectPage::sendCapture(sony::CaptureRequest request) {
    auto previous = state_.capture;
    if (!previous.canRequest(request)) return;
    const bool movie = request.action == CaptureAction::Start || request.action == CaptureAction::Stop;
    if (movie) lockRecording(true);
    if (connection_.capture(request)) {
        captureNotice_.clear();
        feedback(true, movie);
        state_.capture.pending = request.action;
        if (movie) {
            if (page_ != Page::Recording) transition(Page::Recording);
            else redraw();
        } else if (request.action == CaptureAction::Set) {
            transition(valueBack_);
        } else redraw();
    } else {
        lockRecording(connection_.state().capture.locked());
    }
}
void CameraConnectPage::openCapture(Page page) {
    captureNotice_.clear();
    connection_.refreshParameters();
    transition(page);
}
void CameraConnectPage::updateCaptureState(const sony::CaptureState& previous) {
    const auto& capture = state_.capture;
    if (capture.recording != previous.recording) captureNotice_.clear();
    if (capture.resultId != previous.resultId && capture.resultId) {
        if (capture.result == 0x2019) captureNotice_ = "相机忙，请稍候";
        else if (capture.result == sony::StateChanged) captureNotice_ = "相机状态未确认";
        else if (capture.result) captureNotice_ = "相机未接受操作";
    }
    if (capture.locked()) {
        const auto changed = [&](Parameter id) {
            auto* a = previous.find(id);
            auto* b = capture.find(id);
            return bool(a) != bool(b) || (a && b && a->current != b->current);
        };
        if (page_ != Page::Recording || capture.recording != previous.recording ||
            capture.pending != previous.pending || capture.resultId != previous.resultId ||
            (capture.recording == Recording::Unknown && state_.phase != sony::ConnectionPhase::Ready) ||
            changed(Parameter::MovieFormat) || changed(Parameter::MovieRate) || changed(Parameter::Battery)) {
            if (page_ != Page::Recording && page_ != Page::Movie) movieBack_ = Page::Console;
            transition(Page::Recording);
        }
        return;
    }
    if (page_ == Page::Recording) {
        transition(state_.phase == sony::ConnectionPhase::Ready ? Page::Movie : Page::Waiting);
        return;
    }
    if (page_ == Page::Parameters && parameterList_) {
        const unsigned scope = parameterBack_ == Page::Movie ? sony::moviePage : sony::photoPage;
        bool changed = capture.resultId != previous.resultId;
        for (size_t i = 0; !changed && i < sony::parameterSpecCount; ++i) {
            const auto& spec = sony::parameterSpecs[i];
            if (!(spec.pages & scope)) continue;
            auto* a = previous.find(spec.id);
            auto* b = capture.find(spec.id);
            changed = bool(a) != bool(b) || (a && b && !(*a == *b));
        }
        if (!changed) return;
        if (lv_obj_is_scrolling(parameterList_)) { parametersDirty_ = true; return; }
        parameterScroll_ = lv_obj_get_scroll_y(parameterList_);
    }
    if (page_ == Page::Value) {
        auto* p = capture.find(editing_);
        if (!p || !p->options()) { transition(valueBack_); return; }
        if (auto* old = previous.find(editing_); old && *old == *p && capture.pending == previous.pending &&
            capture.resultId == previous.resultId) return;
        auto* old = previous.find(editing_);
        selection_ = old && selection_ < old->options() ? p->indexOf(old->option(selection_)) : p->indexOf(p->current);
    }
    if (page_ == Page::Photo || page_ == Page::Movie || page_ == Page::Parameters || page_ == Page::Value) redraw();
}
void CameraConnectPage::captureAction(int command) {
    if (command == ShootPage || command == MoviePage) {
        const bool movie = command == MoviePage;
        if (movie) movieBack_ = page_ == Page::Photo ? Page::Photo : Page::Console;
        feedback(true);
        openCapture(movie ? Page::Movie : Page::Photo);
    } else if (command == ParametersPage) {
        parameterBack_ = page_;
        parameterScroll_ = 0;
        feedback(true);
        openCapture(Page::Parameters);
    } else if (command >= EditBase) {
        auto id = static_cast<Parameter>(command - EditBase);
        auto* p = state_.capture.find(id);
        if (!p || !p->options() || state_.capture.pending != CaptureAction::None) return;
        valueBack_ = page_;
        if (page_ == Page::Parameters) parameterScroll_ = lv_obj_get_scroll_y(parameterList_);
        editing_ = id;
        selection_ = p->indexOf(p->current);
        feedback(true);
        openCapture(Page::Value);
    } else if (page_ == Page::Value && state_.capture.pending == CaptureAction::None) {
        auto* p = state_.capture.find(editing_);
        if (!p || !p->options()) return;
        if (command == WheelReleased) {
            finishWheelDrag();
        } else if (command == ApplyValue) {
            if (!wheelDragging_ && selection_ < p->options())
                sendCapture({CaptureAction::Set, editing_, p->type, p->option(selection_)});
        } else {
            auto next = selection_;
            if (command == PreviousValue && next && next < p->options()) --next;
            if (command == NextValue) next = next == p->options() ? 0 : next + (next + 1 < p->options());
            if (next != selection_) {
                selection_ = next;
                captureNotice_.clear();
                feedback(true);
                settleWheel(command == NextValue ? -wheelStep : wheelStep);
            } else {
                settleWheel(0);
            }
        }
    }
}
}
