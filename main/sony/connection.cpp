#include "connection.h"
#include "tunnel.h"
#include <algorithm>
#include <optional>
#include <atomic>
#include <cstring>
#include <mutex>
#include <memory>
#include <utility>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <wifi_manager.h>

namespace sony {
namespace {
constexpr char settingsNs[] = "sony_cam";
constexpr char profileKey[] = "profile_v1";
constexpr char presetKey[] = "preset_v1";
constexpr char portal[] = R"html(<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>相机配网</title>
<style>body{background:#181d25;color:#f3f6fc;font:18px system-ui;margin:24px auto;padding:0 20px;max-width:380px}
form{display:grid;gap:14px}input,button{display:block;box-sizing:border-box;width:100%;padding:12px;border-radius:10px;border:0;font:inherit}
input{margin-top:6px;background:#202631;color:#f3f6fc}button{background:#9dc6ff;color:#081627;font-weight:bold}</style>
<h1>手机辅助配网</h1><form method="post" action="/" autocomplete="off">
<label>相机热点<input name="ssid" required maxlength="32"></label>
<label>Wi-Fi 密码<input name="wifi" required minlength="8" maxlength="64" autocomplete="off"></label>
<label>SSH 用户名<input name="user" required maxlength="128" autocomplete="off"></label>
<label>SSH 密码<input name="password" required maxlength="256" autocomplete="off"></label>
<button>提交并核对指纹</button></form></html>)html";

struct StoredProfile {
    char ssid[33], wifi[65], user[129], password[257], pin[51];
    ~StoredProfile() { explicit_bzero(this, sizeof *this); }
};
bool loadProfile(CameraCredentials& out) {
    StoredProfile data{};
    Settings nvs(settingsNs);
    size_t length = sizeof data;
    if (nvs.GetBlob(profileKey, &data, &length) != ESP_OK || length != sizeof data ||
        data.ssid[32] || data.wifi[64] || data.user[128] || data.password[256] || data.pin[50]) return false;
    CameraCredentials profile{data.ssid, data.wifi, data.user, data.password, data.pin};
    if (!profile.valid() || profile.fingerprint.size() != 50 || profile.fingerprint.find("SHA256:") != 0) return false;
    out.clear();
    out = profile;
    return true;
}
bool saveProfile(const CameraCredentials& in) {
    StoredProfile data{};
    memcpy(data.ssid, in.ssid.data(), in.ssid.size());
    memcpy(data.wifi, in.wifiPassword.data(), in.wifiPassword.size());
    memcpy(data.user, in.user.data(), in.user.size());
    memcpy(data.password, in.sshPassword.data(), in.sshPassword.size());
    memcpy(data.pin, in.fingerprint.data(), in.fingerprint.size());
    Settings nvs(settingsNs, true);
    return nvs.SetBlob(profileKey, &data, sizeof data) == ESP_OK && nvs.Commit() == ESP_OK;
}
std::shared_ptr<const Preset> loadPreset(const char* model) {
    auto preset = std::make_shared<Preset>();
    Settings nvs(settingsNs);
    size_t size = sizeof(Preset);
    if (nvs.GetBlob(presetKey, preset.get(), &size) != ESP_OK || size != sizeof(Preset) ||
        !preset->validFor(model)) return {};
    return preset;
}
bool savePreset(const Preset& preset) {
    Settings nvs(settingsNs, true);
    return nvs.SetBlob(presetKey, &preset, sizeof preset) == ESP_OK && nvs.Commit() == ESP_OK;
}
struct AutoCommand { AutoAction action; IntervalPlan plan; std::shared_ptr<const Preset> expected; };
struct Runtime {
    std::mutex mutex;
    ConnectionState state;
    CameraCredentials submitted;
    CameraCredentials retryProfile;
    std::atomic_bool cancelled{false};
    std::atomic_bool confirmed{false};
    bool running = false;
    std::optional<CaptureRequest> request;
    std::optional<AutoCommand> autoCommand;
    unsigned autoEpoch = 0;
    bool refreshRequested = false;
    BodyQueue body;
    unsigned mediaEpoch = 0;
    MediaWindow mediaWindow{};
    bool mediaWarm = false, mediaBrowsing = false;
    std::array<std::shared_ptr<const MediaItem>, 4> mediaDeletes{};
    TaskHandle_t sessionTask = nullptr;
    Tunnel* tunnel = nullptr; // protected by mutex, cleared before destruction
};
Runtime& runtime() { static Runtime value; return value; }
bool automationAllowed(void* context) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    return !r.cancelled && r.autoEpoch == *static_cast<const unsigned*>(context) &&
           r.state.automation.active();
}
void resetMedia(Runtime& r) {
    ++r.mediaEpoch;
    r.mediaWindow = {};
    r.mediaDeletes = {};
    r.state.media = {};
}
void startMedia(Runtime& r) {
    if (r.state.media.status == MediaState::Closed) {
        ++r.mediaEpoch;
        r.state.media.status = MediaState::Loading;
        ++r.state.revision;
    }
    if (r.sessionTask) xTaskNotifyGive(r.sessionTask);
}

void publish(ConnectionState state) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.cancelled) return;
    if (state.phase != ConnectionPhase::Ready) state.capture.uncertain = r.state.capture.locked();
    state.revision = r.state.revision + 1;
    r.state = std::move(state);
}
ConnectionState phase(ConnectionPhase kind, std::string message = {}) {
    ConnectionState next;
    next.phase = kind;
    next.message = std::move(message);
    return next;
}
void delay(unsigned ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
void readBody(const Session& session, BodyState& body) {
    const uint16_t* ids;
    body.buttons.clear();
    body.dials.clear();
    for (size_t i = 0, n = session.buttons(ids); i < n; ++i)
        if (bodyKeyAllowed(ids[i])) body.buttons.push_back(ids[i]);
    for (size_t i = 0, n = session.dials(ids); i < n; ++i)
        if (bodyDialAllowed(ids[i])) body.dials.push_back(ids[i]);
}
void resources(const char* where) {
    constexpr unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI("SonyConnection", "%s heap=%u min=%u largest=%u psram=%u psramMin=%u dma=%u stack=%u",
             where, unsigned(heap_caps_get_free_size(internal)), unsigned(heap_caps_get_minimum_free_size(internal)),
             unsigned(heap_caps_get_largest_free_block(internal)), unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
             unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
bool phoneSetup(WifiManager& wifi, CameraCredentials& out) {
    auto& r = runtime();
    if (!wifi.StartCustomConfigAp("ToolBox-Camera", portal, [](std::string_view form) {
            CameraCredentials credentials;
            if (!readProvisionForm(form, credentials)) return false;
            auto& r = runtime();
            std::lock_guard<std::mutex> lock(r.mutex);
            if (r.cancelled || !r.submitted.ssid.empty()) return false;
            r.submitted = credentials;
            return true;
        })) return false;
    auto stage = phase(ConnectionPhase::Phone);
    stage.ap = wifi.GetApSsid();
    stage.url = wifi.GetApWebUrl();
    publish(std::move(stage));
    int64_t until = esp_timer_get_time() + 180000000;
    while (!r.cancelled && esp_timer_get_time() < until) {
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            if (!r.submitted.ssid.empty()) {
                out = r.submitted;
                break;
            }
        }
        delay(60);
    }
    wifi.StopConfigAp(); // outside the HTTP task and after its response
    { std::lock_guard<std::mutex> lock(r.mutex); r.submitted.clear(); }
    return !r.cancelled && out.valid();
}
ConnectionState runSession(WifiManager& wifi, CameraCredentials& profile, bool& retry, bool& shouldSave) {
    auto& r = runtime();
    auto setup = phase(ConnectionPhase::Connecting, "正在加入相机热点");
    setup.ssid = profile.ssid;
    publish(setup);
    if (!wifi.StartDirectStation(profile.ssid, profile.wifiPassword))
        return phase(ConnectionPhase::Failed, "无法启动相机 Wi-Fi");
    struct Station {
        WifiManager& wifi;
        ~Station() { wifi.StopStation(); }
    } station{wifi};
    for (int i = 0; i < 150 && !r.cancelled && !wifi.IsConnected(); ++i) delay(100);
    if (r.cancelled) return {};
    if (!wifi.IsConnected()) return phase(ConnectionPhase::Failed, "找不到相机或无法加入热点");
    auto* iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    if (!iface || esp_netif_get_ip_info(iface, &info) != ESP_OK || !info.gw.addr)
        return phase(ConnectionPhase::Failed, "未取得相机网关");
    resources("STA");
    setup.message = "正在验证相机身份";
    publish(setup);
    Tunnel tunnel;
    tunnel.cancelWhen(r.cancelled);
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.cancelled) return {};
        r.tunnel = &tunnel;
    }
    struct ActiveTunnel {
        Runtime& runtime;
        ~ActiveTunnel() { std::lock_guard<std::mutex> lock(runtime.mutex); runtime.tunnel = nullptr; }
    } active{r};
    if (!tunnel.handshake(info.gw.addr)) return phase(ConnectionPhase::Failed, "SSH 握手失败");
    if (r.cancelled) return {};
    resources("SSH");
    if (profile.fingerprint != tunnel.fingerprint()) {
        auto check = phase(ConnectionPhase::Verify);
        check.ssid = profile.ssid;
        check.fingerprint = tunnel.fingerprint();
        check.fingerprintChanged = !profile.fingerprint.empty();
        r.confirmed = false;
        publish(std::move(check));
        int64_t until = esp_timer_get_time() + 120000000;
        while (!r.cancelled && !r.confirmed && wifi.IsConnected() && esp_timer_get_time() < until) delay(50);
        if (r.cancelled) return {};
        if (!r.confirmed || !wifi.IsConnected()) return phase(ConnectionPhase::Failed, "指纹验证已中止");
        profile.fingerprint = tunnel.fingerprint();
        shouldSave = true;
    }
    setup.message = "正在建立相机会话";
    publish(setup);
    if (!tunnel.authenticate(profile.fingerprint.c_str(), profile.user.c_str(), profile.sshPassword.c_str()))
        return phase(ConnectionPhase::Failed, "相机拒绝身份验证");
    constexpr size_t capacity = 64 * 1024;
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> data(
        static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), heap_caps_free);
    if (!data) return phase(ConnectionPhase::Failed, "相机会话内存不足");
    uint8_t guid[16] = {'T','o','o','l','B','o','x','O','S',0};
    if (esp_read_mac(guid + 10, ESP_MAC_WIFI_STA) != ESP_OK || r.cancelled) return {};
    Session session(tunnel, data.get(), capacity);
    Parameters initial;
    int rc = session.start(guid, collectParameter, &initial);
    if (rc) { session.close(); return phase(ConnectionPhase::Failed, "相机协议握手失败"); }
    if (r.cancelled) { session.close(); return {}; }
    auto ready = phase(ConnectionPhase::Ready);
    ready.model = session.model();
    ready.ssid = profile.ssid;
    CaptureControl controls(session, std::move(initial), [] { return GetHAL().millis(); });
    Automation automation(session, controls, [] { return GetHAL().millis(); }, savePreset, loadPreset(session.model()));
    ready.automation = automation.state();
    ready.capture = controls.state();
    ready.mediaAvailable = session.supportsMedia();
    ready.mediaDeleteAvailable = session.supportsMediaDelete();
    BodyState body;
    readBody(session, body);
    ready.body = body;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.sessionTask = xTaskGetCurrentTaskHandle();
        if (ready.capture.recording == Recording::Unknown) ready.capture.uncertain = r.state.capture.locked();
    }
    if (shouldSave) {
        if (saveProfile(profile)) shouldSave = false;
        else ready.message = "凭据保存失败";
    }
    resources("ready");
    publish(std::move(ready));
    std::optional<MediaWorker> media;
    unsigned mediaEpoch = 0;
    unsigned autoEpoch = 0;
    bool deletingMedia = false;
    while (!r.cancelled && wifi.IsConnected()) {
        std::optional<CaptureRequest> request;
        std::optional<BodyCommand> command;
        std::optional<AutoCommand> autoCommand;
        bool refresh, more;
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            request.swap(r.request);
            autoCommand.swap(r.autoCommand);
            if (autoCommand) autoEpoch = r.autoEpoch;
            refresh = std::exchange(r.refreshRequested, false);
            if (!request) command = r.body.pop(GetHAL().millis());
        }
        if (request && !r.cancelled) controls.execute(*request);
        if (command && !r.cancelled) {
            int result = StateChanged;
            if (!controls.state().locked()) {
                result = command->kind == BodyCommand::Button ? session.pressButton(command->id)
                                                              : session.turnDial(command->id, command->steps);
            }
            body.result = result;
            ++body.resultId;
        }
        struct MediaEvents { MediaWorker* worker; bool changed = false; } events{media ? &*media : nullptr};
        if (!controls.tick(refresh, [](const Event& e, void* context) {
            auto& events = *static_cast<MediaEvents*>(context);
            if (events.worker && events.worker->event(e)) events.changed = true;
        }, &events)) break;
        if (autoCommand && automationAllowed(&autoEpoch)) {
            if (autoCommand->action == AutoAction::ApplyPreset && autoCommand->expected != automation.state().preset) {
                std::lock_guard<std::mutex> lock(r.mutex);
                if (r.autoEpoch == autoEpoch && r.state.automation.active()) {
                    r.state.automation.result = AutoResult::Failed;
                    r.state.automation.error = StateChanged;
                    ++r.state.revision;
                }
                automation.stop();
            } else {
                automation.start(autoCommand->action, autoCommand->plan);
                resources("automation-start");
            }
        }
        const bool automationChanged = automation.tick(automationAllowed, &autoEpoch) || bool(autoCommand);
        if (automationChanged && !automation.state().active()) resources("automation-end");
        readBody(session, body);
        MediaWindow window{};
        std::shared_ptr<const MediaItem> erase;
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            auto capture = controls.state();
            if (capture.recording == Recording::Unknown && r.state.capture.uncertain) capture.uncertain = true;
            if (capture.locked()) {
                r.body.clear();
                if (r.state.media.status != MediaState::Closed) {
                    resetMedia(r);
                    r.mediaBrowsing = r.mediaWarm = false;
                    ++r.state.revision;
                }
            }
            more = r.body.size() != 0 || bool(r.request);
            if (automationChanged && !r.cancelled && r.autoEpoch == autoEpoch && r.state.automation.active()) {
                r.state.automation = automation.state();
                ++r.state.revision;
            }
            if (!r.cancelled && r.state.automation.preset != automation.state().preset) {
                r.state.automation.preset = automation.state().preset;
                ++r.state.revision;
            }
            if (!r.cancelled && !r.request && !(capture == r.state.capture && body == r.state.body)) {
                r.state.capture = std::move(capture);
                r.state.body = body;
                ++r.state.revision;
            }
            if (mediaEpoch != r.mediaEpoch) {
                media.reset();
                deletingMedia = false;
                mediaEpoch = r.mediaEpoch;
                if (r.state.media.status != MediaState::Closed && !r.cancelled) {
                    media.emplace();
                    media->begin();
                }
            }
            if (media) {
                window = r.mediaWindow;
                const auto& items = media->state().items;
                if (!window[0].slot && (r.mediaWarm || r.mediaBrowsing) && items && !items->empty() &&
                    !r.mediaDeletes.front()) window[0] = items->front().key;
                if (!deletingMedia) erase = r.mediaDeletes.front();
            }
        }
        bool mediaWork = false;
        if (media) {
            bool changed = bool(erase) || events.changed;
            if (erase) {
                media->erase(std::move(erase));
                deletingMedia = true;
            }
            if (request && request->action == CaptureAction::Photo && !controls.state().result && !deletingMedia) {
                media->invalidate(true);
                changed = true;
            }
            if (!more && !r.cancelled && !automation.state().active() && !controls.state().locked() &&
                (!deletingMedia || media->deleting())) {
                struct Permit { unsigned epoch; const CaptureState& capture; } permit{mediaEpoch, controls.state()};
                changed = media->step(session, window, [](void* context) {
                    const auto& permit = *static_cast<Permit*>(context);
                    auto& r = runtime();
                    std::lock_guard<std::mutex> lock(r.mutex);
                    return !r.cancelled && !r.state.automation.active() && r.mediaEpoch == permit.epoch && !r.request &&
                           (!permit.capture.canMovie || permit.capture.recording == Recording::Idle);
                }, &permit) || changed;
            }
            const bool deleteFinished = deletingMedia && !media->deleting();
            mediaWork = media->hasWork(window) ||
                        (changed && !window[0].slot && media->state().items && !media->state().items->empty());
            if (changed || deleteFinished) {
                std::lock_guard<std::mutex> lock(r.mutex);
                if (!r.cancelled && mediaEpoch == r.mediaEpoch) {
                    auto next = media->state();
                    if (next.status == MediaState::Ready && r.state.media.status == MediaState::Loading)
                        resources("media-index");
                    if (std::any_of(next.tiles.begin(), next.tiles.end(), [](const MediaTile& tile) {
                        return bool(tile.image.pixels);
                    }) && std::none_of(r.state.media.tiles.begin(), r.state.media.tiles.end(), [](const MediaTile& tile) {
                        return bool(tile.image.pixels);
                    })) resources("media-thumbnail");
                    next.pendingDeletes = r.state.media.pendingDeletes;
                    next.deleteRevision = r.state.media.deleteRevision;
                    next.deleteResult = r.state.media.deleteResult;
                    next.deleteError = r.state.media.deleteError;
                    if (deleteFinished) {
                        deletingMedia = false;
                        std::move(r.mediaDeletes.begin() + 1, r.mediaDeletes.end(), r.mediaDeletes.begin());
                        r.mediaDeletes.back().reset();
                        std::move(next.pendingDeletes.begin() + 1, next.pendingDeletes.end(), next.pendingDeletes.begin());
                        next.pendingDeletes.back() = {};
                        next.deleteResult = next.status;
                        next.deleteError = next.error;
                        ++next.deleteRevision;
                        mediaWork = mediaWork || bool(r.mediaDeletes.front());
                    }
                    r.state.media = std::move(next);
                    if (!r.mediaWarm && !r.mediaBrowsing && !r.mediaDeletes.front()) resetMedia(r);
                    ++r.state.revision;
                }
            }
        }
        if (!more) {
            if (mediaWork) vTaskDelay(1);
            else ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30));
        }
    }
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.request.reset();
        r.autoCommand.reset();
        ++r.autoEpoch;
        r.sessionTask = nullptr;
        r.body.clear();
        resetMedia(r);
        r.mediaBrowsing = r.mediaWarm = false;
        r.refreshRequested = false;
        if (!r.cancelled) {
            const bool uncertain = r.state.capture.locked() || controls.state().locked();
            const unsigned revision = r.state.revision + 1;
            r.state = phase(ConnectionPhase::Connecting, "正在重连相机");
            r.state.capture.uncertain = uncertain;
            r.state.revision = revision;
        }
    }
    session.close();
    retry = !r.cancelled;
    return phase(ConnectionPhase::Failed, "相机连接已断开");
}

struct Job { ConnectionPhase operation; CameraCredentials profile; };
ConnectionState run(Job& job) {
    auto& hal = GetHAL();
    auto& wifi = WifiManager::GetInstance();
    auto& r = runtime();
    if (r.cancelled) return {};
    if (!hal.acquireCameraWifi()) return phase(ConnectionPhase::Failed, "Wi-Fi 正在使用");
    struct Lease { Hal& hal; ~Lease() { hal.releaseCameraWifi(); resources("released"); } } lease{hal};
    resources("entry");
    if (job.operation == ConnectionPhase::Scanning) {
        auto results = phase(ConnectionPhase::ScanDone);
        for (const auto& ap : wifi.ScanNearby()) {
            const auto* name = reinterpret_cast<const char*>(ap.ssid);
            std::string ssid(name, strnlen(name, sizeof(ap.ssid)));
            if (ssid.find("DIRECT-") == 0 && validCredential(0, ssid) &&
                std::find(results.cameras.begin(), results.cameras.end(), ssid) == results.cameras.end())
                results.cameras.push_back(std::move(ssid));
        }
        return results;
    }
    if (job.operation == ConnectionPhase::Phone && !phoneSetup(wifi, job.profile))
        return phase(ConnectionPhase::Failed, "手机配网已结束");
    if (r.cancelled) return {};
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.cancelled) return {};
        r.retryProfile = job.profile;
    }
    bool shouldSave = job.profile.fingerprint.empty();
    for (;;) {
        bool retry = false;
        auto result = runSession(wifi, job.profile, retry, shouldSave);
        if (!retry || r.cancelled) return result;
        publish(phase(ConnectionPhase::Connecting, "正在重连相机"));
        for (int wait = 0; wait < 10 && !r.cancelled; ++wait) delay(100);
        if (r.cancelled) return {};
    }
}
} // namespace

Connection& Connection::instance() { static Connection instance; return instance; }
bool Connection::hasSaved() const { CameraCredentials profile; return loadProfile(profile); }
ConnectionState Connection::state() const {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.state;
}
unsigned Connection::revision() const {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.state.revision;
}
bool Connection::launch(ConnectionPhase operation, CameraCredentials credentials) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.running) return false;
    r.cancelled = r.confirmed = false;
    r.submitted.clear();
    r.retryProfile.clear();
    auto work = std::make_unique<Job>(Job{operation, credentials});
    const auto revision = r.state.revision + 1;
    const bool uncertain = operation == ConnectionPhase::Connecting && r.state.capture.locked();
    r.state = phase(operation == ConnectionPhase::Phone ? ConnectionPhase::Connecting : operation);
    r.state.capture.uncertain = uncertain;
    r.state.ssid = credentials.ssid;
    r.state.revision = revision;
    r.running = true;
    if (xTaskCreate([](void* context) {
            {
                std::unique_ptr<Job> work(static_cast<Job*>(context));
                auto next = run(*work);
                auto& r = runtime();
                std::lock_guard<std::mutex> lock(r.mutex);
                r.submitted.clear();
                if (r.cancelled) next = {};
                else if (next.phase == ConnectionPhase::Failed) next.capture.uncertain = r.state.capture.locked();
                next.revision = r.state.revision + 1;
                r.state = std::move(next);
                r.running = false;
            }
            vTaskDelete(nullptr);
        }, "sony-camera", 16384, work.get(), 4, nullptr) != pdPASS) {
        r.running = false;
        r.state = phase(ConnectionPhase::Failed, "无法启动相机任务");
        r.state.capture.uncertain = uncertain;
        r.state.revision = revision;
        return false;
    }
    work.release();
    return true;
}
bool Connection::scan() { return launch(ConnectionPhase::Scanning); }
bool Connection::phone() { return launch(ConnectionPhase::Phone); }
bool Connection::connect(CameraCredentials profile) {
    return profile.valid() && launch(ConnectionPhase::Connecting, profile);
}
bool Connection::connectSaved() {
    CameraCredentials profile;
    return loadProfile(profile) && connect(profile);
}
bool Connection::retry() {
    CameraCredentials profile;
    {
        auto& r = runtime();
        std::lock_guard<std::mutex> lock(r.mutex);
        profile = r.retryProfile;
    }
    return profile.valid() && connect(profile);
}
void Connection::confirmFingerprint(std::string_view shown) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.state.phase == ConnectionPhase::Verify && !r.cancelled && r.state.fingerprint == shown) r.confirmed = true;
}
bool Connection::capture(CaptureRequest request) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.cancelled || r.state.phase != ConnectionPhase::Ready || !r.state.capture.canRequest(request) ||
        (r.state.automation.active() && request.action != CaptureAction::Stop)) return false;
    r.request = request;
    r.state.capture.pending = request.action;
    r.state.capture.result = 0;
    ++r.state.revision;
    if (r.sessionTask) xTaskNotifyGive(r.sessionTask);
    return true;
}
void Connection::refreshParameters() {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.state.phase == ConnectionPhase::Ready && !r.cancelled) r.refreshRequested = true;
}
bool Connection::body(BodyCommand command) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    const auto& state = r.state;
    if (r.cancelled || state.phase != ConnectionPhase::Ready || state.capture.locked() ||
        state.automation.active()) return false;
    const bool offered = command.kind == BodyCommand::Button ? state.body.has(BodyKey(command.id))
                                                             : state.body.has(BodyDial(command.id));
    if (!offered) return false;
    command.at = GetHAL().millis();
    return r.body.push(command);
}
void Connection::clearBody() {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.body.clear();
}
void Connection::prepareMedia() {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.cancelled || r.state.phase != ConnectionPhase::Ready || r.state.capture.locked() ||
        !r.state.mediaAvailable) return;
    r.mediaWarm = true;
    startMedia(r);
}
bool Connection::openMedia(bool refresh) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.cancelled || r.state.phase != ConnectionPhase::Ready || r.state.capture.locked() ||
        !r.state.mediaAvailable) return false;
    if (refresh && r.mediaDeletes.front()) return false;
    if (refresh) resetMedia(r);
    r.mediaBrowsing = true;
    startMedia(r);
    return true;
}
void Connection::closeMedia(bool keepWarm) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.mediaWarm = keepWarm && !r.state.capture.locked() && r.state.phase == ConnectionPhase::Ready;
    r.mediaBrowsing = false;
    r.mediaWindow = {};
    if (!r.mediaWarm && !r.mediaDeletes.front()) {
        resetMedia(r);
        ++r.state.revision;
    }
}
void Connection::showMedia(MediaWindow window) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.mediaBrowsing && r.mediaWindow != window) {
        r.mediaWindow = window;
        if (r.sessionTask) xTaskNotifyGive(r.sessionTask);
    }
}
bool Connection::deleteMedia(std::shared_ptr<const MediaItem> item) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto& media = r.state.media;
    if (r.cancelled || !r.mediaBrowsing || !r.state.mediaDeleteAvailable || r.state.capture.locked() || !item ||
        item->protectedItem || r.state.automation.active() || r.mediaDeletes.back() ||
        !media.items || media.status == MediaState::ListFailed ||
        std::find(media.pendingDeletes.begin(), media.pendingDeletes.end(), item->key) != media.pendingDeletes.end())
        return false;
    const auto* actual = findMedia(*media.items, item->key);
    if (!actual || !actual->sameObject(*item)) return false;
    const auto slot = std::find(r.mediaDeletes.begin(), r.mediaDeletes.end(), nullptr) - r.mediaDeletes.begin();
    media.pendingDeletes[slot] = item->key;
    r.mediaDeletes[slot] = std::make_shared<MediaItem>(*item);
    ++r.state.revision;
    if (r.sessionTask) xTaskNotifyGive(r.sessionTask);
    return true;
}
bool Connection::automate(AutoAction action, IntervalPlan plan, std::shared_ptr<const Preset> expected) {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto& state = r.state;
    const auto& capture = state.capture;
    if (r.cancelled || state.phase != ConnectionPhase::Ready || state.automation.active() ||
        capture.recording != Recording::Idle || capture.locked() || capture.pending != CaptureAction::None ||
        r.mediaDeletes.front()) return false;
    const auto& preset = state.automation.preset;
    if (action == AutoAction::None ||
        (action == AutoAction::Interval && (!state.mediaAvailable || !canRunInterval(capture))) ||
        (action == AutoAction::SavePreset && !canSavePreset(capture)) ||
        (action == AutoAction::ApplyPreset && (!preset || preset != expected || !preset->matches(capture)))) return false;
    ++r.autoEpoch;
    r.body.clear();
    r.autoCommand = AutoCommand{action, plan, preset};
    state.automation = {action, AutoResult::Running, 0, 0, action == AutoAction::Interval ? plan.shots :
                        action == AutoAction::ApplyPreset ? preset->count : 0, preset};
    ++state.revision;
    if (r.sessionTask) xTaskNotifyGive(r.sessionTask);
    return true;
}
void Connection::cancelAutomation() {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (!r.state.automation.active()) return;
    r.autoCommand.reset();
    ++r.autoEpoch;
    r.state.automation.result = AutoResult::Stopped;
    ++r.state.revision;
    if (r.sessionTask) xTaskNotifyGive(r.sessionTask);
}
void Connection::stop() {
    auto& r = runtime();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.cancelled = true;
    r.request.reset();
    r.autoCommand.reset();
    ++r.autoEpoch;
    r.body.clear();
    resetMedia(r);
    r.mediaBrowsing = r.mediaWarm = false;
    r.refreshRequested = false;
    r.retryProfile.clear();
    r.submitted.clear();
    if (r.tunnel) r.tunnel->interrupt();
    unsigned revision = r.state.revision + 1;
    r.state = {};
    r.state.revision = revision;
}
Feedback loadFeedback() {
    Settings settings(settingsNs);
    uint8_t flags = 3;
    size_t size = sizeof flags;
    if (settings.GetBlob("feedback", &flags, &size) != ESP_OK || size != sizeof flags) flags = 3;
    return {(flags & 1) != 0, (flags & 2) != 0};
}
void saveFeedback(Feedback value) {
    Settings settings(settingsNs, true);
    uint8_t flags = (value.sound ? 1 : 0) | (value.vibration ? 2 : 0);
    if (settings.SetBlob("feedback", &flags, sizeof flags) == ESP_OK) settings.Commit();
}
}
