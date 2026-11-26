#pragma once
#include "capture.h"
#include "media.h"
#include <optional>

namespace sony {
constexpr unsigned automationLimit = 999;
enum class AutoAction { None, Interval, SavePreset, ApplyPreset };
enum class AutoResult { Idle, Running, Done, Stopped, Failed };
enum AutomationError { PresetStorage = -7, ShotUnconfirmed = -8, SingleShotRequired = -9 };
struct IntervalPlan { unsigned seconds = 5, shots = 30; };
struct PresetEntry {
    int64_t value;
    Parameter id;
    uint16_t type;
};
struct Preset {
    static constexpr uint32_t format = 1;
    uint32_t version = format;
    uint32_t mode = 0;
    char model[128]{};
    uint32_t count = 0;
    std::array<PresetEntry, 17> entries{};
    bool validFor(std::string_view camera) const;
    bool matches(const CaptureState&) const;
};
struct AutomationState {
    AutoAction action = AutoAction::None;
    AutoResult result = AutoResult::Idle;
    int error = 0;
    unsigned completed = 0, total = 0;
    std::shared_ptr<const Preset> preset;
    bool active() const { return result == AutoResult::Running; }
};
bool canSavePreset(const CaptureState&);
bool supportsInterval(const CaptureState&);
bool canRunInterval(const CaptureState&);

// Runs on the same foreground session worker as capture and media.
class Automation {
public:
    using Permit = bool (*)(void*);
    using Save = bool (*)(const Preset&);
    Automation(Session&, CaptureControl&, CaptureControl::Clock, Save, std::shared_ptr<const Preset>);
    void start(AutoAction, IntervalPlan = {});
    void stop();
    bool tick(Permit, void*); // true when the published state changed
    const AutomationState& state() const { return state_; }
private:
    enum Step { Baseline, BeforeShot, Fire, Verify };
    struct Card {
        uint64_t date = 0;
        std::vector<uint32_t> ids;
        void add(const MediaItem&);
        bool contains(const MediaItem&) const;
    };
    Session& session_;
    CaptureControl& capture_;
    CaptureControl::Clock now_;
    Save save_;
    AutomationState state_;
    IntervalPlan plan_;
    std::optional<MediaWorker> baseline_;
    std::array<Card, 2> cards_;
    MediaList page_;
    Step step_ = Baseline;
    unsigned slot_ = 1, referenceSlot_ = 0;
    uint32_t due_ = 0, shotAt_ = 0, verifySince_ = 0;
    unsigned presetIndex_ = 0, resultId_ = 0;
    bool presetRead_ = false, waitingForValue_ = false;
    bool interval(Permit, void*);
    bool preset(Permit, void*);
    int readCard(bool verify, unsigned& photos);
    bool finish(AutoResult, int error = 0);
};
}
