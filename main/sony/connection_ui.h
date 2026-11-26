#pragma once
#include "connection.h"
#include "keypad.h"
#include <lvgl.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace toolbox {
void signGlyph(lv_obj_t* parent, int w, int h, bool plus);
class CameraConnectPage {
public:
    enum class Result { Continue, Ble, Exit };
    CameraConnectPage();
    ~CameraConnectPage();
    Result update(bool keyA, bool keyB, uint32_t now);
    bool recordingLocked() const { return state_.capture.locked(); }
private:
    enum class Page { Entry, Pair, Phone, Scan, Keyboard, Waiting, Fingerprint, Console, Feedback, Error,
                      Photo, Movie, Parameters, Value, Recording, Remote, Dials, Media, Gallery, DeleteMedia,
                      Automation, Interval, Preset, PresetConfirm };
    enum Action {
        None, Back, Wifi, Ble, Settings, Phone, Scan, Retry, Reconfigure, Confirm,
        Sound, Vibration, Finish, Erase, Extra, ShootPage, MoviePage, ParametersPage,
        PreviousValue, NextValue, ApplyValue, WheelReleased, RemotePage, DialsPage,
        PickBase = 30, ModeBase = 70, LetterBase = 80, EditBase = 100,
        LatestMedia = 0x10000, MediaListPage, MediaLeftOrUp, MediaRightOrDown, AskDelete, ConfirmDelete, RefreshMedia,
        MediaPickBase, KeyBase = 0x20000, AutomationPage = 0x30000, IntervalPage, PresetPage, ReviewPreset,
        ShorterInterval, LongerInterval, FewerShots, MoreShots, StartInterval, SavePreset, ApplyPreset
    };
    static void onClick(lv_event_t* event);
    Result action(int command, uint32_t now, bool key);
    void request(bool started, Page next);
    void transition(Page next);
    void redraw();
    void refreshInput();
    void beginField();
    void feedback(bool keyB, bool silent = false);
    void drawCapture();
    void drawParameters();
    void drawValue();
    void drawRecording();
    void drawCameraStatus(int y);
    void updateClock(uint32_t now);
    void updateRecording(uint32_t now);
    void updateCaptureState(const sony::CaptureState& previous);
    void lockRecording(bool locked);
    void drawRemote();
    void drawDials();
    void drawBodyNotice(int y);
    void drawConsoleTiles();
    bool offersInterval() const;
    bool offersPreset() const;
    static bool automationPage(Page);
    void drawAutomation();
    void updateAutomation(const sony::ConnectionState& previous);
    void updateInterval();
    void automationAction(int command, bool key);
    void keyButton(sony::BodyKey key, const char* name, int x, int y, int w, int h, const lv_font_t* font);
    bool sendBody(sony::BodyCommand command);
    void leaveBody(Page next);
    void updateBodyState(const sony::BodyState& previous);
    void rotateWheel(int steps);
    static void controlWheelTouch(lv_event_t* event);
    static void dialStripTouch(lv_event_t* event);
    static void dialStepPress(lv_event_t* event);
    void shiftStrip(int index, int shift);
    static bool mediaPage(Page);
    void mediaAction(int command);
    void drawMediaTile(int x, int y, int w, int h);
    static void playbackGlyph(lv_obj_t* parent, int x, int y, int w = 26, int h = 20);
    void drawMedia();
    void drawMediaContent();
    void updateMedia(const sony::MediaState& previous);
    void updateMediaImages();
    void clearMediaViews();
    void requestMediaImages();
    void drawMediaSlides();
    void drawMediaSlide(size_t selection, int side);
    void drawMediaImage(size_t view, const sony::MediaItem&, lv_obj_t* box, int w, int h);
    size_t mediaNeighbor(int side) const;
    void trackMediaTouch(lv_obj_t*);
    static void mediaTouch(lv_event_t*);
    static void animateMediaOffset(void*, int32_t offset);
    void moveMedia(int offset);
    void settleMedia(int side);
    void stopMediaSwipe();
    const sony::MediaItem* selectedMedia() const;
    void indexMedia();
    const sony::MediaItem& mediaItem(size_t index) const;
    void captureAction(int command);
    void sendCapture(sony::CaptureRequest request);
    void openCapture(Page page);
    void captureTile(const char* name, const lv_image_dsc_t* glyph, int x, int y, int w, int h, int cmd);
    std::string captureValue(sony::Parameter id) const;
    static void wheelTouch(lv_event_t* event);
    void showWheelValues(size_t center);
    void dragWheel(int delta);
    void finishWheelDrag();
    void moveWheel(int offset);
    void settleWheel(int offset);
    void stopWheelAnimation();
    lv_obj_t* label(const char* value, int x, int y, int w, int h, const lv_font_t* font,
                    uint32_t color = 0xf3f6fc, bool wrap = false, lv_obj_t* parent = nullptr);
    lv_obj_t* button(const char* value, int x, int y, int w, int h, int command,
                     uint32_t background, const lv_font_t* font, int radius = 24,
                     uint32_t color = 0xf3f6fc, lv_obj_t* parent = nullptr);
    lv_obj_t* cornerButton(int x, int command, lv_obj_t* parent = nullptr, int parentY = 0);
    lv_obj_t* menu(lv_obj_t* list, const char* caption, int y, int command, const char* value = nullptr);
    void tile(const char* name, const lv_image_dsc_t* glyph, int y, int h, int command);
    void bind_command(lv_obj_t* object, int command);
    void bottom(const char* name, int command);
    void title(const char* name, bool back = false);
    void drawKeyboard();
    static constexpr int cornerSize = 48, cornerTop = 84, cornerLeft = 58, cornerRight = 466 - cornerLeft - cornerSize;
    static constexpr int confirmLeft = 82, confirmRight = 240, confirmTop = 348, confirmWidth = 144, confirmHeight = 57;
    lv_obj_t* root_;
    lv_obj_t* input_ = nullptr;
    lv_obj_t* keyboardHint_ = nullptr;
    std::atomic_int pending_{None};
    Page page_ = Page::Entry;
    Page settingsBack_ = Page::Entry;
    Page parameterBack_ = Page::Photo, valueBack_ = Page::Photo, movieBack_ = Page::Console;
    sony::Parameter editing_{};
    size_t selection_ = 0;
    lv_obj_t* parameterList_ = nullptr;
    int parameterScroll_ = 0;
    bool parametersDirty_ = false;
    lv_obj_t* wheelTrack_ = nullptr;
    lv_obj_t* applyButton_ = nullptr;
    lv_obj_t* applyHint_ = nullptr;
    std::array<lv_obj_t*, 5> wheelValues_{};
    size_t wheelCenter_ = 0;
    bool wheelDragging_ = false, wheelMoved_ = false;
    int wheelY_ = 0, wheelOffset_ = 0;
    sony::WheelGesture controlWheel_;
    lv_obj_t* wheelIndicator_ = nullptr;
    int wheelAngle_ = 0;
    std::array<std::array<lv_obj_t*, 12>, 2> stripLines_{};
    std::array<int, 2> stripShift_{};
    int stripX_ = 0, stripStepX_ = 0;
    lv_obj_t* bodyNotice_ = nullptr;
    bool bodyDirty_ = false;
    Page mediaBack_ = Page::Console;
    int64_t mediaReference_ = 0;
    size_t mediaSelection_ = 0;
    std::vector<size_t> mediaOrder_;
    bool mediaFollowLatest_ = false;
    std::shared_ptr<const sony::MediaItem> deleteTarget_;
    lv_obj_t* mediaContent_ = nullptr;
    lv_obj_t* mediaViewport_ = nullptr;
    std::array<lv_obj_t*, 3> mediaSlides_{};
    struct MediaView {
        lv_obj_t* region = nullptr;
        lv_obj_t* image = nullptr;
        lv_obj_t* placeholder = nullptr;
        sony::MediaKey key;
        sony::MediaImage pixels;
        lv_image_dsc_t descriptor{};
    };
    std::array<MediaView, 12> mediaViews_{};
    lv_point_t mediaTouchAt_{};
    int mediaOffset_ = 0, mediaSettleSide_ = 0;
    bool mediaTracking_ = false, mediaSwiped_ = false, mediaWrongAxis_ = false, mediaSettling_ = false;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* watchBattery_ = nullptr;
    lv_obj_t* watchPercent_ = nullptr;
    uint32_t clockReadAt_ = 0;
    lv_obj_t* timer_ = nullptr;
    uint32_t shownSeconds_ = UINT32_MAX, activityAt_ = 0;
    bool mute_ = false;
    std::string captureNotice_;
    sony::IntervalPlan intervalPlan_;
    std::array<lv_obj_t*, 4> intervalSteps_{};
    lv_obj_t *intervalSeconds_ = nullptr, *intervalShots_ = nullptr, *intervalCount_ = nullptr, *intervalHint_ = nullptr;
    std::shared_ptr<const sony::Preset> reviewedPreset_;
    bool autoNotice_ = false;
    sony::Connection& connection_;
    sony::ConnectionState state_;
    bool watching_ = false;
    bool firstPoll_ = false;
    bool fromPhone_ = false;
    sony::CameraCredentials credentials_;
    sony::Keypad keypad_;
    sony::Feedback feedback_;
    unsigned field_ = 0;
};
}
