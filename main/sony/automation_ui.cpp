#include "connection_ui.h"
#include <system/ui.h>
#include <algorithm>

namespace toolbox {
namespace {
using sony::AutoAction;
using sony::AutoResult;
using sony::AutomationState;
constexpr uint32_t card = 0x181d25, control = 0x202631, muted = 0x98a4b7;
constexpr uint32_t blue = 0x9dc6ff, ink = 0x081627;
const char* failure(int error) {
    if (error == 0x2019) return "相机忙，操作停止";
    if (error == sony::PresetStorage) return "预设保存失败";
    if (error == sony::ShotUnconfirmed) return "未确认照片写入";
    if (error == sony::SingleShotRequired) return "请使用单张模式并退出 BULB";
    return "相机结果未确认";
}
}

bool CameraConnectPage::offersInterval() const {
    return state_.mediaAvailable && sony::supportsInterval(state_.capture);
}
bool CameraConnectPage::offersPreset() const {
    return sony::canSavePreset(state_.capture) || bool(state_.automation.preset);
}
bool CameraConnectPage::automationPage(Page page) {
    return page == Page::Automation || page == Page::Interval || page == Page::Preset || page == Page::PresetConfirm;
}
void CameraConnectPage::drawAutomation() {
    const auto& state = state_.automation;
    if (page_ == Page::Automation) {
        title("自动化", true);
        int y = 142;
        if (offersInterval()) {
            button("间隔拍摄", 70, y, 326, 68, IntervalPage, card, &toolbox_misans_26);
            y += 80;
        }
        if (offersPreset()) button("参数预设", 70, y, 326, 68, PresetPage, card, &toolbox_misans_26);
        bottom("返回", Back);
    } else if (page_ == Page::Interval) {
        title("间隔拍摄", true);
        label("拍摄间隔", 100, 110, 266, 36, &toolbox_misans_20, muted);
        const auto step = [&](int x, int y, int cmd, bool plus) {
            auto* box = button("", x, y, 64, 56, cmd, control, &toolbox_misans_20, 18);
            signGlyph(box, 64, 56, plus);
            return box;
        };
        intervalSteps_[0] = step(82, 151, ShorterInterval, false);
        intervalSteps_[1] = step(320, 151, LongerInterval, true);
        auto* seconds = ui::row(root_, 151, 147, 164, 65, 6);
        intervalSeconds_ = ui::row_text(seconds, "", &toolbox_misans_semibold_36, 0xf3f6fc);
        ui::row_text(seconds, "秒", &toolbox_misans_20, 0xf3f6fc);
        label("拍摄张数", 100, 222, 266, 36, &toolbox_misans_20, muted);
        intervalSteps_[2] = step(82, 261, FewerShots, false);
        intervalSteps_[3] = step(320, 261, MoreShots, true);
        auto* shots = ui::row(root_, 151, 257, 164, 65, 6);
        intervalShots_ = ui::row_text(shots, "", &toolbox_misans_semibold_36, 0xf3f6fc);
        ui::row_text(shots, "张", &toolbox_misans_20, 0xf3f6fc);
        intervalCount_ = label("", 85, 325, 296, 39, &toolbox_misans_20, blue);
        intervalHint_ = label("", 88, 366, 290, 39, &toolbox_misans_20, muted);
        updateInterval();
    } else if (page_ == Page::Preset) {
        title("参数预设", true);
        if (sony::canSavePreset(state_.capture)) {
            auto* save = button(state.preset ? "覆盖为相机当前参数" : "保存相机当前参数",
                                70, 146, 326, 75, SavePreset, card, &toolbox_misans_26);
            if (state.active()) lv_obj_add_state(save, LV_STATE_DISABLED);
        }
        if (state.preset) {
            auto* view = button("查看并应用预设", 70, 237, 326, 75, ReviewPreset, card, &toolbox_misans_26);
            if (state.active()) lv_obj_add_state(view, LV_STATE_DISABLED);
        } else label("暂无已保存预设", 70, 237, 326, 75, &toolbox_misans_20, muted);
        if (autoNotice_) label("相机暂不可用", 88, 320, 290, 42, &toolbox_misans_20, muted);
        else if (state.action == AutoAction::SavePreset) {
            const char* hint = state.active() ? "正在读取相机参数" : state.result == AutoResult::Done ?
                              "预设已保存" : state.result == AutoResult::Failed ? failure(state.error) : "";
            label(hint, 88, 320, 290, 42, &toolbox_misans_20, muted);
        }
        bottom("返回", Back);
    } else if (reviewedPreset_) {
        title("应用预设？");
        const auto& preset = *reviewedPreset_;
        const auto mode = "机身模式 " + sony::parameterText(sony::Parameter::Mode, preset.mode);
        label(mode.c_str(), 88, 88, 290, 32, &toolbox_misans_20, muted);
        auto* list = ui::menu_list(root_, 128, 156);
        for (unsigned i = 0; i < preset.count; ++i) {
            const auto& entry = preset.entries[i];
            const auto value = sony::parameterText(entry.id, entry.value);
            menu(list, sony::parameterName(entry.id), i * ui::menu_pitch, None, value.c_str());
        }
        const bool matches = reviewedPreset_ == state.preset && preset.matches(state_.capture);
        std::string hint = matches ? "" : "预设与当前机身模式不匹配";
        if (autoNotice_) hint = "相机暂不可用";
        else if (state.action == AutoAction::ApplyPreset) {
            if (state.active()) hint = "正在应用";
            else if (state.result == AutoResult::Done) hint = "已核对 " + std::to_string(state.completed) +
                                                             " / " + std::to_string(state.total) + " 项";
            else if (state.result == AutoResult::Failed) hint = failure(state.error);
        }
        label(hint.c_str(), 78, 294, 310, 40, &toolbox_misans_20, muted);
        button(state.active() ? "停止" : state.action == AutoAction::ApplyPreset &&
               state.result == AutoResult::Done ? "返回" : "取消",
               confirmLeft, confirmTop, confirmWidth, confirmHeight, Back, control, &toolbox_misans_26);
        if (!state.active() && matches)
            button("应用", confirmRight, confirmTop, confirmWidth, confirmHeight, ApplyPreset, blue, &toolbox_misans_26, 24, ink);
    }
}
void CameraConnectPage::updateInterval() {
    const auto& state = state_.automation;
    const bool run = state.action == AutoAction::Interval && state.active();
    const unsigned done = state.action == AutoAction::Interval ? state.completed : 0;
    const unsigned settings[] = {intervalPlan_.seconds, intervalPlan_.shots};
    ui::set_text_fmt(intervalSeconds_, "%u", settings[0]);
    ui::set_text_fmt(intervalShots_, "%u", settings[1]);
    const unsigned total = state.action == AutoAction::Interval ? state.total : settings[1];
    ui::set_text_fmt(intervalCount_, "已完成 %u / %u 张", done, total);
    const char* hint = "KEYB 开始";
    if (autoNotice_) hint = "相机暂不可用";
    else if (run) hint = "KEYA 停止";
    else if (!sony::canRunInterval(state_.capture)) {
        const auto* drive = state_.capture.find(sony::Parameter::Drive);
        const auto* shutter = state_.capture.find(sony::Parameter::Shutter);
        hint = drive && drive->current != 1 ? "请在机身选择单张拍摄" :
               shutter && !shutter->current ? "请在机身退出 BULB" : "相机暂不可用";
    } else if (state.action == AutoAction::Interval) {
        if (state.result == AutoResult::Failed) hint = failure(state.error);
        else if (state.result == AutoResult::Stopped) hint = "已停止 · KEYB 重新开始";
        else if (state.result == AutoResult::Done) hint = "拍摄完成 · KEYB 重新开始";
    }
    ui::set_text(intervalHint_, hint);
    for (unsigned i = 0; i < intervalSteps_.size(); ++i)
        lv_obj_set_state(intervalSteps_[i], LV_STATE_DISABLED,
                         run || (i % 2 ? settings[i / 2] == sony::automationLimit : settings[i / 2] == 1));
}
void CameraConnectPage::updateAutomation(const sony::ConnectionState& previous) {
    const auto& a = state_.automation;
    const auto& b = previous.automation;
    if (page_ == Page::Interval) {
        if (a.result != b.result || a.completed != b.completed || a.error != b.error ||
            sony::canRunInterval(state_.capture) != sony::canRunInterval(previous.capture)) updateInterval();
    } else if (page_ == Page::PresetConfirm && reviewedPreset_) {
        if (a.result != b.result || a.preset != b.preset ||
            reviewedPreset_->matches(state_.capture) != reviewedPreset_->matches(previous.capture)) redraw();
    } else if (page_ == Page::Preset) {
        if (a.result != b.result || a.preset != b.preset ||
            sony::canSavePreset(state_.capture) != sony::canSavePreset(previous.capture)) redraw();
    } else if (page_ == Page::Console || page_ == Page::Automation) {
        const bool offeredInterval = previous.mediaAvailable && sony::supportsInterval(previous.capture);
        const bool offeredPreset = previous.automation.preset || sony::canSavePreset(previous.capture);
        if (offersInterval() != offeredInterval || offersPreset() != offeredPreset) redraw();
    }
}
void CameraConnectPage::automationAction(int cmd, bool key) {
    if (cmd == Back) {
        feedback(!key);
        if (state_.automation.active()) {
            connection_.cancelAutomation();
            state_.automation = connection_.state().automation;
            if (page_ == Page::Interval) updateInterval();
            else redraw();
        } else transition(page_ == Page::Automation ? Page::Console :
                          page_ == Page::PresetConfirm ? Page::Preset : Page::Automation);
        return;
    }
    if (state_.automation.active()) return;
    if (cmd == AutomationPage && page_ == Page::Console && (offersInterval() || offersPreset())) {
        feedback(true);
        connection_.refreshParameters();
        transition(Page::Automation);
    } else if (cmd == IntervalPage && page_ == Page::Automation && offersInterval()) {
        feedback(true);
        transition(Page::Interval);
    } else if (cmd == PresetPage && page_ == Page::Automation && offersPreset()) {
        feedback(true);
        transition(Page::Preset);
    } else if (cmd == ReviewPreset && page_ == Page::Preset && state_.automation.preset) {
        feedback(true);
        reviewedPreset_ = state_.automation.preset;
        transition(Page::PresetConfirm);
    } else if (cmd >= ShorterInterval && cmd <= MoreShots && page_ == Page::Interval) {
        feedback(true);
        auto& value = cmd <= LongerInterval ? intervalPlan_.seconds : intervalPlan_.shots;
        value = std::clamp(int(value) + ((cmd - ShorterInterval) % 2 ? 1 : -1), 1, int(sony::automationLimit));
        autoNotice_ = false;
        updateInterval();
    } else {
        AutoAction action = AutoAction::None;
        if (cmd == StartInterval && page_ == Page::Interval && sony::canRunInterval(state_.capture))
            action = AutoAction::Interval;
        if (cmd == SavePreset && page_ == Page::Preset && sony::canSavePreset(state_.capture))
            action = AutoAction::SavePreset;
        if (cmd == ApplyPreset && page_ == Page::PresetConfirm && reviewedPreset_ &&
            reviewedPreset_ == state_.automation.preset && reviewedPreset_->matches(state_.capture))
            action = AutoAction::ApplyPreset;
        if (action == AutoAction::None) return;
        feedback(true);
        autoNotice_ = !connection_.automate(action, intervalPlan_, reviewedPreset_);
        state_.automation = connection_.state().automation;
        if (page_ == Page::Interval) updateInterval();
        else redraw();
    }
}
}
