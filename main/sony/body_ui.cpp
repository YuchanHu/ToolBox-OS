#include "connection_ui.h"
#include <hal/hal.h>
#include <system/ui.h>
#include <cstdlib>

namespace toolbox {
namespace {
using sony::BodyCommand;
using sony::BodyDial;
using sony::BodyKey;
constexpr uint32_t card = 0x181d25, control = 0x202631, muted = 0x98a4b7, blue = 0x9dc6ff;
constexpr uint32_t text = 0xf3f6fc, groove = 0x303a49, stripe = 0x405064, pressed = 0x344861;
constexpr int wheelCenter = 109, stripPitch = 14, stripStepPixels = 28, stripCycle = 12 * stripPitch;
// Direction marks 66–75 px from the wheel centre, in Up, Down, Left, Right order like BodyKey 1–4.
constexpr lv_point_precise_t chevrons[4][3] = {
    {{100, 43}, {109, 34}, {118, 43}},
    {{118, 175}, {109, 184}, {100, 175}},
    {{43, 118}, {34, 109}, {43, 100}},
    {{175, 100}, {184, 109}, {175, 118}},
};
constexpr BodyDial stripDials[2] = {BodyDial::Front, BodyDial::Rear};

void drawPlayTriangle(lv_event_t* event) {
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_current_target_obj(event), &area);
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.color = lv_color_hex(text);
    triangle.opa = LV_OPA_COVER;
    const int w = lv_area_get_width(&area), h = lv_area_get_height(&area);
    triangle.p[0] = {area.x1 + w * 9 / 26, area.y1 + h / 4};
    triangle.p[1] = {area.x1 + w * 9 / 26, area.y1 + h - h / 4};
    triangle.p[2] = {area.x1 + w * 17 / 26, area.y1 + h / 2};
    lv_draw_triangle(lv_event_get_layer(event), &triangle);
}
}
// Drawn − and + keep step keys identical on every page instead of depending on font glyphs.
void signGlyph(lv_obj_t* parent, int w, int h, bool plus) {
    ui::panel(parent, w / 2 - 10, h / 2 - 1, 20, 3, text, 1);
    if (plus) ui::panel(parent, w / 2 - 2, h / 2 - 9, 3, 20, text, 1);
}
namespace {
lv_obj_t* stepButton(lv_obj_t* parent, int x, int y, bool plus) {
    auto* box = ui::panel(parent, x, y, 68, 61, control, 18);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(box, lv_color_hex(pressed), LV_STATE_PRESSED);
    signGlyph(box, 68, 61, plus);
    return box;
}
}

// Sony's playback mark: a framed play triangle.
void CameraConnectPage::playbackGlyph(lv_obj_t* parent, int x, int y, int w, int h) {
    auto* frame = ui::panel(parent, x, y, w, h, 0, 3);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(text), 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_add_event_cb(frame, drawPlayTriangle, LV_EVENT_DRAW_MAIN_END, nullptr);
}

void CameraConnectPage::keyButton(BodyKey key, const char* name, int x, int y, int w, int h, const lv_font_t* font) {
    button(name, x, y, w, h, KeyBase + int(key), control, font, h > 47 ? 18 : 24);
}
void CameraConnectPage::drawBodyNotice(int y) {
    bodyNotice_ = label("", 114, y, 238, 28, &toolbox_misans_20, muted);
}
void CameraConnectPage::drawRemote() {
    const auto& body = state_.body;
    title("机身控制", true);
    // The pair starts right of the round back control so their touch areas never overlap.
    if (body.has(BodyKey::Menu)) keyButton(BodyKey::Menu, "MENU", 112, 99, 114, 47, &toolbox_misans_20);
    if (body.has(BodyKey::Fn)) keyButton(BodyKey::Fn, "Fn", 240, 99, 114, 47, &toolbox_misans_20);
    const bool turn = body.has(BodyDial::ControlWheel);
    bool keys = body.has(BodyKey::Set);
    for (uint16_t id = 1; id <= 4; ++id) keys = keys || body.has(BodyKey(id));
    if (keys || turn) {
        auto* wheel = ui::panel(root_, 124, 151, 218, 218, card, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(wheel, lv_obj_flag_t(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
        for (auto code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
            lv_obj_add_event_cb(wheel, controlWheelTouch, code, this);
        auto* ring = ui::panel(wheel, 10, 10, 198, 198, 0, LV_RADIUS_CIRCLE);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(groove), 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        if (turn) {
            wheelIndicator_ = lv_arc_create(wheel);
            lv_obj_remove_style_all(wheelIndicator_);
            lv_obj_remove_flag(wheelIndicator_, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(wheelIndicator_, 9, 9);
            lv_obj_set_size(wheelIndicator_, 200, 200);
            lv_obj_set_style_arc_opa(wheelIndicator_, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(wheelIndicator_, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(wheelIndicator_, lv_color_hex(blue), LV_PART_INDICATOR);
            lv_obj_set_style_arc_width(wheelIndicator_, 3, LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(wheelIndicator_, true, LV_PART_INDICATOR);
            lv_arc_set_bg_angles(wheelIndicator_, 0, 360);
            rotateWheel(0);
        }
        auto* hub = ui::panel(wheel, 65, 65, 88, 88, control, LV_RADIUS_CIRCLE);
        for (uint16_t id = 1; id <= 4; ++id) {
            if (!body.has(BodyKey(id))) continue;
            auto* mark = lv_line_create(wheel);
            lv_obj_remove_style_all(mark);
            lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
            lv_line_set_points(mark, chevrons[id - 1], 3);
            lv_obj_set_style_line_width(mark, 3, 0);
            lv_obj_set_style_line_color(mark, lv_color_hex(text), 0);
            lv_obj_set_style_line_rounded(mark, true, 0);
        }
        if (body.has(BodyKey::Set)) label("SET", 0, 0, 88, 88, &toolbox_misans_20, text, false, hub);
    }
    const bool playback = body.has(BodyKey::Playback);
    bool dials = body.has(BodyDial::Front) || body.has(BodyDial::Rear) || body.has(BodyKey::AfOn);
    for (auto key : {BodyKey::C1, BodyKey::C2, BodyKey::C3}) dials = dials || body.has(key);
    if (playback) {
        auto* box = button("回放", dials ? 94 : 166, 372, 134, 45, KeyBase + int(BodyKey::Playback), control,
                           &toolbox_misans_20);
        lv_obj_align(lv_obj_get_child(box, 0), LV_ALIGN_CENTER, 17, 0);
        playbackGlyph(box, 30, 12);
    }
    if (dials) button("拨轮", playback ? 238 : 166, 372, 134, 45, DialsPage, control, &toolbox_misans_20);
    drawBodyNotice(420);
}
void CameraConnectPage::drawDials() {
    const auto& body = state_.body;
    title("拨轮 / 自定义键", true);
    for (int i = 0; i < 2; ++i) {
        const auto dial = stripDials[i];
        if (!body.has(dial)) continue;
        const int y = 151 + i * 111;
        label(i ? "后拨轮" : "前拨轮", 132, y - 33, 202, 30, &toolbox_misans_20, muted);
        for (bool plus : {false, true}) {
            auto* step = stepButton(root_, plus ? 323 : 75, y, plus);
            lv_obj_set_user_data(step, reinterpret_cast<void*>(intptr_t(plus ? int(dial) : -int(dial))));
            for (auto code : {LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED, LV_EVENT_LONG_PRESSED_REPEAT})
                lv_obj_add_event_cb(step, dialStepPress, code, this);
        }
        auto* strip = ui::panel(root_, 153, y, 160, 61, control, 18);
        lv_obj_add_flag(strip, lv_obj_flag_t(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
        lv_obj_set_user_data(strip, reinterpret_cast<void*>(intptr_t(i)));
        for (auto code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED})
            lv_obj_add_event_cb(strip, dialStripTouch, code, this);
        for (auto*& line : stripLines_[i]) line = ui::panel(strip, 0, 0, 2, 61, stripe);
        shiftStrip(i, stripShift_[i]);
    }
    struct Custom { BodyKey key; const char* name; };
    constexpr Custom customs[] = {{BodyKey::C1, "C1"}, {BodyKey::C2, "C2"}, {BodyKey::C3, "C3"}, {BodyKey::AfOn, "AF-ON"}};
    int count = 0;
    for (const auto& c : customs) count += body.has(c.key);
    int x = 233 - (count * 73 + (count - 1) * 6) / 2;
    for (const auto& c : customs) {
        if (!body.has(c.key)) continue;
        keyButton(c.key, c.name, x, 345, 73, 50, &toolbox_misans_18);
        x += 79;
    }
    drawBodyNotice(410);
}
void CameraConnectPage::shiftStrip(int index, int shift) {
    stripShift_[index] = shift;
    for (int k = 0; k < int(stripLines_[index].size()); ++k) {
        auto* line = stripLines_[index][k];
        if (!line) return;
        const int x = ((k * stripPitch + shift) % stripCycle + stripCycle) % stripCycle;
        // Stripes stay clear of the rounded ends instead of paying for corner clipping.
        lv_obj_set_flag(line, LV_OBJ_FLAG_HIDDEN, x < 8 || x > 150);
        lv_obj_set_x(line, x);
    }
}
void CameraConnectPage::rotateWheel(int steps) {
    if (steps && !sendBody({BodyCommand::Dial, uint16_t(BodyDial::ControlWheel), int16_t(steps)})) return;
    wheelAngle_ = ((wheelAngle_ + steps * sony::WheelGesture::stepDegrees) % 360 + 360) % 360;
    // 270° is 12 o'clock for LVGL arcs; the mark spans the prototype's 35°.
    if (wheelIndicator_) lv_arc_set_angles(wheelIndicator_, (253 + wheelAngle_) % 360, (287 + wheelAngle_) % 360);
}
bool CameraConnectPage::sendBody(BodyCommand command) {
    if (!connection_.body(command)) return false;
    if (bodyNotice_) ui::set_text(bodyNotice_, "");
    feedback(true);
    return true;
}
void CameraConnectPage::leaveBody(Page next) {
    connection_.clearBody();
    controlWheel_.cancel();
    transition(next);
}
void CameraConnectPage::controlWheelTouch(lv_event_t* event) {
    auto& page = *static_cast<CameraConnectPage*>(lv_event_get_user_data(event));
    auto* input = lv_indev_active();
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_current_target_obj(event), &area);
    const int x = point.x - area.x1 - wheelCenter, y = point.y - area.y1 - wheelCenter;
    auto& gesture = page.controlWheel_;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED: gesture.press(x, y); break;
    case LV_EVENT_PRESSING: if (gesture.active()) page.rotateWheel(gesture.move(x, y)); break;
    case LV_EVENT_RELEASED: {
        if (!gesture.active()) break;
        page.rotateWheel(gesture.move(x, y));
        const auto tap = gesture.release(x, y);
        // WheelTap Up…Set share BodyKey's 1–5 numbering.
        const auto key = BodyKey(uint16_t(tap));
        if (tap != sony::WheelTap::None && page.state_.body.has(key))
            page.sendBody({BodyCommand::Button, uint16_t(key)});
        break;
    }
    case LV_EVENT_PRESS_LOST: gesture.cancel(); break;
    default: break;
    }
}
void CameraConnectPage::dialStripTouch(lv_event_t* event) {
    auto& page = *static_cast<CameraConnectPage*>(lv_event_get_user_data(event));
    auto* input = lv_indev_active();
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    const int index = int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        page.stripX_ = page.stripStepX_ = point.x;
        return;
    }
    page.shiftStrip(index, page.stripShift_[index] + point.x - page.stripX_);
    page.stripX_ = point.x;
    // Each 28 px of horizontal travel is one detent; rightward turns clockwise.
    const int steps = (point.x - page.stripStepX_) / stripStepPixels;
    if (!steps) return;
    page.stripStepX_ += steps * stripStepPixels;
    page.sendBody({BodyCommand::Dial, uint16_t(stripDials[index]), int16_t(steps)});
}
void CameraConnectPage::dialStepPress(lv_event_t* event) {
    auto& page = *static_cast<CameraConnectPage*>(lv_event_get_user_data(event));
    const int value = int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
    page.sendBody({BodyCommand::Dial, uint16_t(std::abs(value)), int16_t(value > 0 ? 1 : -1)});
}
void CameraConnectPage::updateBodyState(const sony::BodyState& previous) {
    const auto& body = state_.body;
    if (page_ == Page::Console && body.any() != previous.any()) redraw();
    if (page_ != Page::Remote && page_ != Page::Dials) return;
    if (body.resultId != previous.resultId && bodyNotice_) {
        const int result = body.result;
        ui::set_text(bodyNotice_, !result ? "" : result == 0x2019 ? "相机忙，请稍候" :
                                  result == sony::StateChanged ? "录像中不可操作" : "相机未接受操作");
    }
    if (body.buttons == previous.buttons && body.dials == previous.dials) return;
    if (!body.any()) { leaveBody(Page::Console); return; }
    if (lv_indev_get_active_obj()) bodyDirty_ = true;
    else redraw();
}
}
