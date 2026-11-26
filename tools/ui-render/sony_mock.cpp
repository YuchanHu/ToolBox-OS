#include "sony_mock.h"
#include <algorithm>

namespace sony {
namespace {
ConnectionState current;
Feedback feedback;
}
namespace preview {
bool saved = false, confirmed = false;
CameraCredentials submitted;
std::vector<CaptureRequest> commands;
std::vector<BodyCommand> bodyCommands;
unsigned bodyClears = 0;
MediaWindow mediaWindow{};
std::shared_ptr<const MediaItem> mediaRemoved;
unsigned mediaCloses = 0;
std::vector<AutoAction> automationCommands;
IntervalPlan intervalPlan;
unsigned automationCancels = 0;
unsigned stateReads = 0;
void report(ConnectionState state) { state.revision = current.revision + 1; current = state; }
}
Connection& Connection::instance() { static Connection value; return value; }
bool Connection::hasSaved() const { return preview::saved; }
ConnectionState Connection::state() const { ++preview::stateReads; return current; }
unsigned Connection::revision() const { return current.revision; }
bool Connection::scan() {
    ConnectionState state;
    state.phase = ConnectionPhase::ScanDone;
    state.cameras = {"DIRECT-TEST:ILCE-7M5", "DIRECT-TEST:FX30"};
    preview::report(state);
    return true;
}
bool Connection::phone() {
    ConnectionState state;
    state.phase = ConnectionPhase::Phone;
    state.ap = "ToolBox-Camera-TEST";
    state.url = "http://192.168.4.1";
    preview::report(state);
    return true;
}
bool Connection::connect(CameraCredentials credentials) {
    preview::submitted = credentials;
    if (!credentials.valid()) return false;
    return connectSaved();
}
bool Connection::connectSaved() {
    ConnectionState state;
    state.phase = ConnectionPhase::Connecting;
    state.ssid = "DIRECT-TEST:ILCE-7M5";
    state.message = "正在验证相机身份";
    preview::report(state);
    return true;
}
bool Connection::retry() { return preview::submitted.valid() && connectSaved(); }
void Connection::confirmFingerprint(std::string_view shown) {
    preview::confirmed = current.phase == ConnectionPhase::Verify && current.fingerprint == shown;
}
bool Connection::capture(CaptureRequest request) {
    if (current.phase != ConnectionPhase::Ready || !current.capture.canRequest(request)) return false;
    preview::commands.push_back(request);
    current.capture.pending = request.action;
    ++current.revision;
    return true;
}
void Connection::refreshParameters() {}
bool Connection::body(BodyCommand command) {
    if (current.phase != ConnectionPhase::Ready || current.capture.locked()) return false;
    const bool offered = command.kind == BodyCommand::Button ? current.body.has(BodyKey(command.id))
                                                             : current.body.has(BodyDial(command.id));
    if (!offered) return false;
    preview::bodyCommands.push_back(command);
    return true;
}
void Connection::clearBody() { ++preview::bodyClears; }
void Connection::prepareMedia() {
    if (current.mediaAvailable && current.media.status == MediaState::Closed) {
        current.media.status = MediaState::Loading;
        ++current.revision;
    }
}
bool Connection::openMedia(bool refresh) {
    if (!current.mediaAvailable || current.capture.locked()) return false;
    if (refresh) current.media = {};
    prepareMedia();
    preview::mediaWindow = {};
    return true;
}
void Connection::closeMedia(bool keepWarm) {
    ++preview::mediaCloses;
    if (!keepWarm) current.media = {};
    ++current.revision;
}
void Connection::showMedia(MediaWindow window) { preview::mediaWindow = window; }
bool Connection::deleteMedia(std::shared_ptr<const MediaItem> item) {
    if (!item || !current.media.items || findMedia(*current.media.items, item->key) != item.get()) return false;
    auto& pending = current.media.pendingDeletes;
    const auto slot = std::find_if(pending.begin(), pending.end(), [](auto key) { return !key.slot; });
    if (slot == pending.end()) return false;
    *slot = item->key;
    preview::mediaRemoved = std::move(item);
    ++current.revision;
    return true;
}
bool Connection::automate(AutoAction action, IntervalPlan plan, std::shared_ptr<const Preset> expected) {
    if (current.phase != ConnectionPhase::Ready || current.automation.active() ||
        (action == AutoAction::ApplyPreset && expected != current.automation.preset)) return false;
    preview::automationCommands.push_back(action);
    preview::intervalPlan = plan;
    auto& state = current.automation;
    state.action = action;
    state.result = AutoResult::Running;
    state.completed = 0;
    state.total = action == AutoAction::Interval ? plan.shots : action == AutoAction::ApplyPreset ? expected->count : 0;
    ++current.revision;
    return true;
}
void Connection::cancelAutomation() {
    if (!current.automation.active()) return;
    ++preview::automationCancels;
    current.automation.result = AutoResult::Stopped;
    ++current.revision;
}
void Connection::stop() { cancelAutomation(); preview::report({}); }
Feedback loadFeedback() { return feedback; }
void saveFeedback(Feedback value) { feedback = value; }
}
