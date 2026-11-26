#include "connection_ui.h"
#include <array>
#include <string>
#include <hal/hal.h>
#include <apps/common/audio/audio.h>
#include <system/ui.h>
#include <system/shell.h>

namespace toolbox {
namespace {
constexpr uint32_t card = 0x181d25, control = 0x202631, muted = 0x98a4b7;
constexpr uint32_t blue = 0x9dc6ff, ink = 0x081627, green = 0x62d9a1;
constexpr size_t fieldLimits[] = {64, 128, 256};
constexpr const char* fields[] = {"Wi-Fi 密码", "SSH 用户名", "SSH 密码"};
}
CameraConnectPage::CameraConnectPage()
    : root_(ui::round_page(lv_screen_active())),
      connection_(sony::Connection::instance()), feedback_(sony::loadFeedback()) { redraw(); }
CameraConnectPage::~CameraConnectPage() {
    if (automationPage(page_)) connection_.cancelAutomation();
    stopMediaSwipe();
    stopWheelAnimation();
    lockRecording(false);
    if (input_) lv_label_set_text(input_, "");
    lv_obj_delete(root_);
    clearMediaViews();
}
lv_obj_t* CameraConnectPage::label(const char* value, int x, int y, int w, int h, const lv_font_t* font,
                                   uint32_t color, bool wrap, lv_obj_t* parent) {
    if (!parent) parent = root_;
    auto* space = ui::panel(parent, x, y, w, h);
    lv_obj_set_style_bg_opa(space, LV_OPA_TRANSP, 0);
    auto* text = ui::text(space, value, 0, 0, w, font, color);
    lv_label_set_long_mode(text, wrap ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(text);
    return text;
}
lv_obj_t* CameraConnectPage::button(const char* value, int x, int y, int w, int h, int command,
                                   uint32_t background, const lv_font_t* font, int radius,
                                   uint32_t color, lv_obj_t* parent) {
    auto* box = ui::button(parent ? parent : root_, value, x, y, w, h, [](lv_event_t*) {}, background, font);
    lv_obj_set_style_radius(box, radius, LV_PART_MAIN);
    auto* caption = lv_obj_get_child(box, 0);
    lv_obj_set_width(caption, w - 12);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(caption, lv_color_hex(color), LV_PART_MAIN);
    if (!background) lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_opa(box, LV_OPA_40, LV_STATE_DISABLED);
    bind_command(box, command);
    return box;
}
void CameraConnectPage::bind_command(lv_obj_t* object, int command) {
    lv_obj_set_user_data(object, reinterpret_cast<void*>(intptr_t(command)));
    lv_obj_add_event_cb(object, onClick, LV_EVENT_CLICKED, this);
}
void CameraConnectPage::onClick(lv_event_t* event) {
    auto* page = static_cast<CameraConnectPage*>(lv_event_get_user_data(event));
    const auto command = int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(event))));
    if (page->page_ == Page::Gallery && command >= MediaPickBase && command < MediaPickBase + 4 &&
        (page->mediaSwiped_ || page->mediaSettling_)) return;
    page->pending_ = command;
}
void CameraConnectPage::title(const char* name, bool back) {
    label(name, 81, 40, 304, 43, &toolbox_misans_26);
    if (back) {
        auto* target = cornerButton(cornerLeft, Back);
        ui::icon(target, &toolbox_icon_back_28, (cornerSize - 28) / 2, (cornerSize - 28) / 2);
    }
}
// Every page shares one round back control below the title; media mirrors it for the album grid.
lv_obj_t* CameraConnectPage::cornerButton(int x, int command, lv_obj_t* parent, int parentY) {
    auto* box = button("", x, cornerTop - parentY, cornerSize, cornerSize, command, card, &toolbox_misans_20,
                       LV_RADIUS_CIRCLE, 0xf3f6fc, parent);
    lv_obj_set_ext_click_area(box, 4);
    return box;
}
lv_obj_t* CameraConnectPage::menu(lv_obj_t* list, const char* caption, int y, int command, const char* value) {
    auto* row = ui::menu_row(list, caption, 0, y, value).object;
    if (command == None) lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    else bind_command(row, command);
    return row;
}
void CameraConnectPage::tile(const char* name, const lv_image_dsc_t* glyph, int y, int h, int command) {
    auto* box = button(name, 70, y, 326, h, command, card, &toolbox_misans_26);
    lv_obj_align(lv_obj_get_child(box, 0), LV_ALIGN_CENTER, 0, 19);
    ui::icon(box, glyph, (326 - glyph->header.w) / 2, 12, 0xf3f6fc);
}
void CameraConnectPage::bottom(const char* name, int command) {
    button(name, 143, 381, 180, 49, command, 0, &toolbox_misans_20, 24, muted);
}
void CameraConnectPage::transition(Page next) {
    if (page_ == Page::Keyboard && next != Page::Keyboard && input_) lv_label_set_text(input_, "");
    const auto capturePage = [](Page p) { return p == Page::Photo || p == Page::Movie; };
    if (mediaPage(page_) && !mediaPage(next)) {
        connection_.closeMedia(capturePage(next));
        state_.media = {};
        mediaOrder_.clear();
        deleteTarget_.reset();
    } else if (!mediaPage(next) && capturePage(page_) && !capturePage(next)) {
        connection_.closeMedia();
    }
    if (capturePage(next)) connection_.prepareMedia();
    if (automationPage(page_) && page_ != next) {
        connection_.cancelAutomation();
        if (page_ == Page::PresetConfirm) reviewedPreset_.reset();
    }
    autoNotice_ = false;
    page_ = next;
    redraw();
}
void CameraConnectPage::redraw() {
    if (input_) lv_label_set_text(input_, "");
    stopMediaSwipe();
    stopWheelAnimation();
    lv_obj_clean(root_);
    mediaContent_ = mediaViewport_ = nullptr;
    mediaSlides_ = {};
    clearMediaViews();
    input_ = keyboardHint_ = nullptr;
    parameterList_ = timer_ = clock_ = watchBattery_ = watchPercent_ = nullptr;
    wheelTrack_ = applyButton_ = applyHint_ = nullptr;
    wheelIndicator_ = bodyNotice_ = nullptr;
    intervalSteps_ = {};
    intervalSeconds_ = intervalShots_ = intervalCount_ = intervalHint_ = nullptr;
    stripLines_ = {};
    controlWheel_.cancel();
    bodyDirty_ = false;
    wheelDragging_ = false;
    parametersDirty_ = false;
    shownSeconds_ = UINT32_MAX;
    switch (page_) {
    case Page::Entry:
        title("连接相机");
        tile("Wi-Fi 高级控制", &toolbox_icon_wifi_28, 138, 90, Wifi);
        tile("蓝牙遥控", &toolbox_icon_ble_28, 240, 90, Ble);
        bottom("反馈设置", Settings);
        break;
    case Page::Pair:
        title("Wi-Fi 配网", true);
        tile("手机辅助", &toolbox_icon_phone_28, 142, 92, Phone);
        tile("本机键盘", &toolbox_icon_wifi_28, 246, 92, Scan);
        bottom("返回", Back);
        break;
    case Page::Phone:
        title("手机辅助配网");
        ui::icon(root_, &toolbox_icon_phone_28, 219, 111, blue);
        label(state_.ap.c_str(), 62, 154, 342, 44, &toolbox_misans_24);
        {
            label("手机连接此热点", 74, 201, 318, 42, &toolbox_misans_20, muted);
            auto url = state_.url;
            if (url.find("http://") == 0) url.erase(0, 7);
            label(url.c_str(), 75, 246, 316, 47, &toolbox_misans_32);
        }
        label("在手机浏览器填写凭据", 76, 299, 314, 40, &toolbox_misans_20, muted);
        bottom("返回", Back);
        break;
    case Page::Scan: {
        title("选择相机", true);
        if (state_.phase == sony::ConnectionPhase::Scanning) {
            label("正在扫描相机热点", 74, 177, 318, 54, &toolbox_misans_26, muted);
        } else if (state_.cameras.empty()) {
            label("未找到相机热点", 74, 168, 318, 51, &toolbox_misans_26);
            label("请打开机身 Wi-Fi Direct 信息页", 69, 236, 328, 54, &toolbox_misans_20, muted, true);
        } else {
            auto* list = ui::menu_list(root_, 140, 198);
            ui::cache_scroll_rows(list);
            for (size_t i = 0; i < state_.cameras.size(); ++i) {
                auto* item = menu(list, state_.cameras[i].c_str(), int(i) * ui::menu_pitch, PickBase + int(i));
                if (i == 0) {
                    lv_obj_set_style_border_color(item, lv_color_hex(blue), LV_PART_MAIN);
                    lv_obj_set_style_border_width(item, 2, LV_PART_MAIN);
                }
            }
        }
        bottom("重新扫描", Scan);
        break;
    }
    case Page::Keyboard:
        drawKeyboard();
        break;
    case Page::Waiting:
        title("正在连接");
        ui::icon(root_, &toolbox_icon_wifi_78, 194, 157, blue);
        label(state_.ssid.c_str(), 68, 244, 330, 46, &toolbox_misans_24);
        label(state_.message.c_str(), 72, 296, 322, 43, &toolbox_misans_20, muted);
        bottom("取消", Back);
        break;
    case Page::Fingerprint: {
        title("核对相机指纹");
        auto* lock = ui::panel(root_, 216, 123, 34, 27, blue, 5);
        auto* loop = ui::panel(root_, 221, 110, 24, 23, 0, 9);
        lv_obj_set_style_border_color(loop, lv_color_hex(blue), 0);
        lv_obj_set_style_border_width(loop, 3, 0);
        lv_obj_move_foreground(lock);
        label("SHA256", 79, 164, 308, 42, &toolbox_misans_20, muted);
        ui::panel(root_, 75, 207, 316, 81, card, 20);
        std::string hash = state_.fingerprint.substr(7);
        if (hash.size() > 22) hash.insert(22, 1, '\n');
        label(hash.c_str(), 83, 207, 300, 81, &toolbox_misans_18, 0xf3f6fc, true);
        label(state_.fingerprintChanged ? "指纹已变化，请与相机核对" : "与相机显示的指纹核对",
              75, 292, 316, 43, &toolbox_misans_20, state_.fingerprintChanged ? blue : muted);
        if (recordingLocked()) label("KEYB 确认指纹", 114, 348, 238, 57, &toolbox_misans_26, blue);
        else {
            button("取消", confirmLeft, confirmTop, confirmWidth, confirmHeight, Back, control, &toolbox_misans_26);
            button("确认", confirmRight, confirmTop, confirmWidth, confirmHeight, Confirm, blue, &toolbox_misans_26, 24, ink);
        }
        break;
    }
    case Page::Console:
        title(state_.model.c_str());
        ui::panel(root_, 138, 102, 10, 10, green, 5);
        label("Wi-Fi 已连接", 154, 90, 155, 34, &toolbox_misans_20, muted);
        drawConsoleTiles();
        if (!state_.message.empty()) label(state_.message.c_str(), 104, 414, 258, 28, &toolbox_misans_20, muted);
        if (offersInterval() || offersPreset()) {
            button("自动化", 108, 370, 118, 44, AutomationPage, 0, &toolbox_misans_20, 24, muted);
            button("设置", 240, 370, 118, 44, Settings, 0, &toolbox_misans_20, 24, muted);
        } else {
            button("设置", 174, 370, 118, 44, Settings, 0, &toolbox_misans_20, 24, muted);
        }
        break;
    case Page::Feedback: {
        title("反馈设置", true);
        auto* sound = button("操作音效", 69, 142, 328, 70, Sound, control, &toolbox_misans_26);
        auto* vibration = button("振动反馈", 69, 224, 328, 70, Vibration, control, &toolbox_misans_26);
        for (auto* item : {sound, vibration}) {
            auto* name = lv_obj_get_child(item, 0);
            lv_obj_set_width(name, 128);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 54, 0);
        }
        ui::icon(sound, &toolbox_icon_sound_28, 22, 21);
        auto* phone = ui::icon(vibration, &toolbox_icon_phone_28, 22, 21);
        lv_image_set_rotation(phone, 150);
        label(feedback_.sound ? "开启" : "关闭", 224, 0, 85, 70, &toolbox_misans_20,
              feedback_.sound ? blue : muted, false, sound);
        label(feedback_.vibration ? "开启" : "关闭", 224, 0, 85, 70, &toolbox_misans_20,
              feedback_.vibration ? blue : muted, false, vibration);
        std::string volume = std::string("系统音量 ") + std::to_string(GetHAL().getSpeakerVolume()) + "%";
        label(volume.c_str(), 92, 304, 282, 43, &toolbox_misans_20, muted);
        bottom("连接设置", Reconfigure);
        break;
    }
    case Page::Error:
        title("连接失败", true);
        label(state_.message.c_str(), 67, 145, 332, 88, &toolbox_misans_26, muted, true);
        button("重新配置", confirmLeft, 257, confirmWidth, confirmHeight, Reconfigure, control, &toolbox_misans_26);
        button("重试", confirmRight, 257, confirmWidth, confirmHeight, Retry, blue, &toolbox_misans_26, 24, ink);
        bottom("返回", Back);
        break;
    case Page::Photo: case Page::Movie: drawCapture(); break;
    case Page::Parameters: drawParameters(); break;
    case Page::Value: drawValue(); break;
    case Page::Recording: drawRecording(); break;
    case Page::Remote: drawRemote(); break;
    case Page::Dials: drawDials(); break;
    case Page::Media: case Page::Gallery: case Page::DeleteMedia: drawMedia(); break;
    case Page::Automation: case Page::Interval: case Page::Preset: case Page::PresetConfirm: drawAutomation(); break;
    }
}
void CameraConnectPage::drawConsoleTiles() {
    // Offered entries fill the 2×2 grid in order, so a hidden capability never leaves a hole.
    struct Entry {
        const char* caption;
        const lv_image_dsc_t* icon;
        Action command;
    };
    Entry entries[4];
    int count = 0;
    if (state_.capture.canPhoto) entries[count++] = {"拍照", &toolbox_camera_console_photo, ShootPage};
    if (state_.capture.canMovie) entries[count++] = {"录像", &toolbox_camera_console_movie, MoviePage};
    if (state_.body.any()) entries[count++] = {"机身", &toolbox_camera_console_body, RemotePage};
    if (state_.mediaAvailable) entries[count++] = {"相册", &toolbox_camera_console_album, LatestMedia};
    constexpr int tileWidth = 156, tileHeight = 104, iconTop = 16;
    const int top = count <= 2 ? 192 : 140;
    for (int i = 0; i < count; ++i) {
        const int x = count % 2 && i == count - 1 ? 155 : 72 + i % 2 * 166;
        const int y = top + i / 2 * 114;
        const auto& entry = entries[i];
        auto* box = button(entry.caption, x, y, tileWidth, tileHeight, entry.command, card, &toolbox_misans_26);
        lv_obj_align(lv_obj_get_child(box, 0), LV_ALIGN_CENTER, 0, 17);
        ui::icon(box, entry.icon, (tileWidth - entry.icon->header.w) / 2, iconTop);
    }
}
void CameraConnectPage::drawKeyboard() {
    title(fields[field_]);
    ui::panel(root_, 70, 89, 326, 48, card, 18);
    input_ = label("", 88, 89, 290, 48, &toolbox_misans_24);
    constexpr std::array<const char*, 4> modes{"abc","ABC","123","符号"};
    for (unsigned i = 0; i < modes.size(); ++i) {
        bool selected = unsigned(keypad_.mode()) == i;
        button(modes[i], 74 + int(i) * 80, 144, 74, 39, ModeBase + i, selected ? blue : control,
               &toolbox_misans_20, 12, selected ? ink : 0xf3f6fc);
    }
    for (unsigned i = 0; i < 9; ++i) {
        auto key = keypad_.key(i);
        if (key.empty()) continue;
        std::string caption = key == " " ? "空格" : std::string(key);
        button(caption.c_str(), 70 + int(i % 3) * 110, 191 + int(i / 3) * 61, 106, 58, LetterBase + i,
               control, &toolbox_misans_26, 15);
    }
    if (unsigned(keypad_.mode()) < 2) {
        button("完成", 108, 378, 140, 46, Finish, blue, &toolbox_misans_20, 17, ink);
        button("退格", 258, 378, 100, 46, Erase, control, &toolbox_misans_20, 15);
    } else {
        button(keypad_.mode() == sony::Keypad::Mode::Number ? "0" : "更多",
               104, 378, 64, 43, Extra, control, &toolbox_misans_20, 15);
        button("完成", 181, 378, 104, 43, Finish, blue, &toolbox_misans_20, 17, ink);
        button("退格", 298, 378, 64, 43, Erase, control, &toolbox_misans_20, 15);
    }
    keyboardHint_ = label("", 101, 425, 264, 25, &toolbox_misans_20, blue);
    refreshInput();
}
void CameraConnectPage::refreshInput() {
    auto value = keypad_.display();
    const auto* font = &toolbox_misans_24;
    // Keep the newest printable ASCII visible without leaving the rounded field.
    std::string_view tail = value;
    lv_point_t size{};
    lv_text_get_size(&size, tail.data(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    while (size.x > 284 && !tail.empty()) {
        tail.remove_prefix(1);
        lv_text_get_size(&size, tail.data(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    }
    ui::set_text(input_, tail.data());
}
void CameraConnectPage::beginField() {
    const std::string* source[] = {&credentials_.wifiPassword, &credentials_.user, &credentials_.sshPassword};
    keypad_.reset(*source[field_], fieldLimits[field_]);
    transition(Page::Keyboard);
}
void CameraConnectPage::feedback(bool keyB, bool silent) {
    auto& hal = GetHAL();
    if (!silent && !mute_ && feedback_.sound && hal.getSpeakerVolume() > 0) audio::play_tone_from_midi(keyB ? 96 : 94);
    if (feedback_.vibration) hal.vibrate(20, 60);
}
void CameraConnectPage::request(bool started, Page next) {
    state_ = connection_.state();
    watching_ = started;
    firstPoll_ = started;
    if (!started && state_.phase != sony::ConnectionPhase::Failed) state_.message = "连接正在关闭，请重试";
    transition(started ? next : Page::Error);
}
CameraConnectPage::Result CameraConnectPage::update(bool keyA, bool keyB, uint32_t now) {
    // update() runs on every main-loop pass; copy the shared state only after the worker publishes.
    if (watching_ && (firstPoll_ || connection_.revision() != state_.revision)) {
        {
            auto previous = state_;
            firstPoll_ = false;
            state_ = connection_.state();
            using Phase = sony::ConnectionPhase;
            if (state_.phase == Phase::Ready && previous.phase != Phase::Ready) credentials_.clear();
            lockRecording(recordingLocked());
            if (recordingLocked() && state_.phase == Phase::Verify) {
                transition(Page::Fingerprint);
            } else if (recordingLocked() || page_ == Page::Recording ||
                (state_.phase == Phase::Ready && previous.phase == Phase::Ready)) {
                updateCaptureState(previous.capture);
                if (!recordingLocked()) updateBodyState(previous.body);
                if (page_ == Page::Console && previous.mediaAvailable != state_.mediaAvailable) redraw();
                if (mediaPage(page_)) updateMedia(previous.media);
                if (!recordingLocked()) updateAutomation(previous);
            } else {
                switch (state_.phase) {
                case Phase::Phone: transition(Page::Phone); break;
                case Phase::ScanDone: watching_ = false; transition(Page::Scan); break;
                case Phase::Connecting: transition(Page::Waiting); break;
                case Phase::Verify: transition(Page::Fingerprint); break;
                case Phase::Ready: transition(Page::Console); break;
                case Phase::Failed: watching_ = false; transition(Page::Error); break;
                default: break;
                }
                pending_ = None;
            }
        }
    }
    updateClock(now);
    if (automationPage(page_) && state_.automation.active() && now - activityAt_ >= 1000) {
        shell_note_activity();
        activityAt_ = now;
    }
    if (parametersDirty_ && parameterList_ && !lv_obj_is_scrolling(parameterList_)) {
        parameterScroll_ = lv_obj_get_scroll_y(parameterList_);
        redraw();
    }
    if (bodyDirty_ && !lv_indev_get_active_obj()) redraw();
    if (recordingLocked()) {
        pending_ = None;
        if (now - activityAt_ >= 1000) {
            shell_note_activity();
            activityAt_ = now;
        }
        if (keyB) {
            if (state_.capture.recording == sony::Recording::Active) sendCapture({sony::CaptureAction::Stop});
            else if (state_.phase == sony::ConnectionPhase::Verify) connection_.confirmFingerprint(state_.fingerprint);
            else if (state_.phase == sony::ConnectionPhase::Failed) {
                if (connection_.retry() || connection_.connectSaved()) watching_ = true;
            }
        }
        updateRecording(now);
        return Result::Continue;
    }
    if (page_ == Page::Keyboard && keypad_.tick(now)) {
        refreshInput();
    }
    int cmd = pending_.exchange(None);
    if (keyA) cmd = Back;
    else if (keyB && cmd != MediaLeftOrUp && cmd != MediaRightOrDown) {
        switch (page_) {
        case Page::Entry: cmd = Wifi; break;
        case Page::Pair: cmd = Scan; break;
        case Page::Scan: cmd = state_.cameras.empty() ? Scan : PickBase; break;
        case Page::Keyboard: cmd = Finish; break;
        case Page::Fingerprint: cmd = Confirm; break;
        case Page::Error: cmd = Retry; break;
        case Page::Console: if (state_.capture.canPhoto) cmd = ShootPage; break;
        case Page::Photo: sendCapture({sony::CaptureAction::Photo}); return Result::Continue;
        case Page::Movie: sendCapture({sony::CaptureAction::Start}); return Result::Continue;
        case Page::Value: cmd = ApplyValue; break;
        case Page::Remote: if (state_.body.has(sony::BodyKey::Set)) cmd = KeyBase + int(sony::BodyKey::Set); break;
        case Page::Media: cmd = AskDelete; break;
        case Page::Gallery: cmd = MediaPickBase + int(mediaSelection_ % 4); break;
        case Page::DeleteMedia: cmd = ConfirmDelete; break;
        case Page::Interval: cmd = StartInterval; break;
        case Page::PresetConfirm: cmd = ApplyPreset; break;
        default: break;
        }
    }
    return action(cmd, now, keyA || keyB);
}
CameraConnectPage::Result CameraConnectPage::action(int cmd, uint32_t now, bool key) {
    if (cmd == None) return Result::Continue;
    if ((cmd == Back && automationPage(page_)) || (cmd >= AutomationPage && cmd <= ApplyPreset)) {
        automationAction(cmd, key);
        return Result::Continue;
    }
    if ((cmd == Back && mediaPage(page_)) || (cmd >= LatestMedia && cmd <= RefreshMedia) ||
        (cmd >= MediaPickBase && cmd < MediaPickBase + 4)) {
        mediaAction(cmd);
        return Result::Continue;
    }
    if (cmd == Back) {
        feedback(!key);
        switch (page_) {
        case Page::Entry: return Result::Exit;
        case Page::Pair: transition(Page::Entry); break;
        case Page::Keyboard:
            if (field_ > 0) { --field_; beginField(); }
            else { keypad_.reset({}, 0); transition(Page::Scan); }
            break;
        case Page::Feedback: transition(settingsBack_); break;
        case Page::Photo: transition(Page::Console); break;
        case Page::Movie: if (movieBack_ == Page::Photo) openCapture(Page::Photo); else transition(Page::Console); break;
        case Page::Parameters: openCapture(parameterBack_); break;
        case Page::Value: transition(valueBack_); break;
        case Page::Remote: leaveBody(Page::Console); break;
        case Page::Dials: leaveBody(Page::Remote); break;
        case Page::Console:
            connection_.stop(); watching_ = false; transition(Page::Entry); break;
        default:
            connection_.stop(); watching_ = false; credentials_.clear(); transition(Page::Pair); break;
        }
        return Result::Continue;
    }
    if (cmd >= KeyBase) {
        if (page_ == Page::Remote || page_ == Page::Dials) sendBody({sony::BodyCommand::Button, uint16_t(cmd - KeyBase)});
        return Result::Continue;
    }
    if ((cmd >= ShootPage && cmd <= WheelReleased) || cmd >= EditBase) {
        captureAction(cmd);
        return Result::Continue;
    }
    if (cmd == Ble) { feedback(true); return Result::Ble; }
    feedback(true);
    switch (cmd) {
    case Wifi:
        fromPhone_ = false;
        if (connection_.hasSaved()) request(connection_.connectSaved(), Page::Waiting);
        else transition(Page::Pair);
        break;
    case Settings: settingsBack_ = page_; transition(Page::Feedback); break;
    case RemotePage:
        if (state_.body.any()) { connection_.refreshParameters(); transition(Page::Remote); }
        break;
    case DialsPage: transition(Page::Dials); break;
    case Phone: fromPhone_ = true; credentials_.clear(); request(connection_.phone(), Page::Waiting); break;
    case Scan: fromPhone_ = false; request(connection_.scan(), Page::Scan); break;
    case Confirm: connection_.confirmFingerprint(state_.fingerprint); break;
    case Reconfigure:
        connection_.stop(); watching_ = false; credentials_.clear();
        transition(page_ == Page::Feedback && settingsBack_ == Page::Entry ? Page::Entry : Page::Pair);
        break;
    case Retry:
        if (fromPhone_) {
            if (connection_.retry()) request(true, Page::Waiting);
            else request(connection_.phone(), Page::Waiting);
        }
        else if (credentials_.valid()) request(connection_.connect(credentials_), Page::Waiting);
        else if (connection_.hasSaved()) request(connection_.connectSaved(), Page::Waiting);
        else transition(Page::Pair);
        break;
    case Sound:
        feedback_.sound = !feedback_.sound; sony::saveFeedback(feedback_); redraw(); break;
    case Vibration:
        feedback_.vibration = !feedback_.vibration; sony::saveFeedback(feedback_); redraw(); break;
    case Erase:
        keypad_.erase(); refreshInput(); ui::set_text(keyboardHint_, ""); break;
    case Extra:
        keypad_.extra();
        if (keypad_.mode() == sony::Keypad::Mode::Symbol) redraw();
        else refreshInput();
        break;
    case Finish: {
        keypad_.commit();
        if (!sony::validCredential(field_ + 1, keypad_.value())) {
            ui::set_text(keyboardHint_, "请检查凭据长度和字符");
            break;
        }
        std::string* dst[] = {&credentials_.wifiPassword, &credentials_.user, &credentials_.sshPassword};
        dst[field_]->assign(keypad_.value());
        if (field_ < 2) { ++field_; beginField(); }
        else { keypad_.reset({}, 0); request(connection_.connect(credentials_), Page::Waiting); }
        break;
    }
    default:
        if (page_ == Page::Keyboard && cmd >= LetterBase && cmd < LetterBase + 9) {
            keypad_.tap(cmd - LetterBase, now);
            refreshInput();
            ui::set_text(keyboardHint_, "");
        } else if (page_ == Page::Keyboard && cmd >= ModeBase && cmd < ModeBase + 4) {
            keypad_.setMode(static_cast<sony::Keypad::Mode>(cmd - ModeBase));
            redraw();
        } else if (page_ == Page::Scan && cmd >= PickBase && size_t(cmd - PickBase) < state_.cameras.size()) {
            credentials_.clear();
            credentials_.ssid = state_.cameras[cmd - PickBase];
            field_ = 0;
            beginField();
        }
        break;
    }
    return Result::Continue;
}
}
