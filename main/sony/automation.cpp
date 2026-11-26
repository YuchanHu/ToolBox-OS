#include "automation.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace sony {
namespace {
constexpr int confirmationTimeout = 120000;
constexpr unsigned scope(uint32_t mode) {
    mode &= 0xffff;
    return mode >= 0x8050 && mode <= 0x8054 ? moviePage : photoPage;
}
unsigned presetScope(Parameter id) {
    for (size_t i = 0; i < parameterSpecCount; ++i)
        if (parameterSpecs[i].id == id) return parameterSpecs[i].pages;
    return 0;
}
bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
bool photo(const MediaItem& item) {
    return !item.dummy && !item.video() && std::any_of(item.files.begin(), item.files.end(), [](const MediaFile& file) {
        const auto type = file.type();
        return type == "JPEG" || type == "RAW" || type == "HEIF";
    });
}
}

bool Preset::validFor(std::string_view camera) const {
    if (version != format || !count || count > entries.size() ||
        camera.empty() || camera.size() >= sizeof model || !memchr(model, 0, sizeof model) || camera != model)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (!(presetScope(entry.id) & scope(mode)) || entry.type < 1 || entry.type > 6) return false;
    }
    return true;
}
bool Preset::matches(const CaptureState& capture) const {
    const auto* current = capture.find(Parameter::Mode);
    return current && current->current == mode;
}
bool canSavePreset(const CaptureState& capture) {
    auto* mode = capture.find(Parameter::Mode);
    if (!mode || !capture.parameters) return false;
    return std::any_of(capture.parameters->begin(), capture.parameters->end(), [mode](const CameraParameter& p) {
        return p.options() && (presetScope(p.id) & scope(uint32_t(mode->current)));
    });
}
bool supportsInterval(const CaptureState& capture) {
    const auto* drive = capture.find(Parameter::Drive);
    return capture.canPhoto && drive && (drive->current == 1 || drive->indexOf(1) < drive->options());
}
bool canRunInterval(const CaptureState& capture) {
    const auto* drive = capture.find(Parameter::Drive);
    const auto* shutter = capture.find(Parameter::Shutter);
    return capture.canRequest({CaptureAction::Photo}) && drive && drive->current == 1 &&
           (!shutter || shutter->current != 0);
}
Automation::Automation(Session& session, CaptureControl& capture, CaptureControl::Clock clock,
                       Save save, std::shared_ptr<const Preset> preset)
    : session_(session), capture_(capture), now_(clock), save_(save) {
    state_.preset = std::move(preset);
}
void Automation::start(AutoAction action, IntervalPlan plan) {
    state_ = {action, AutoResult::Running, 0, 0, 0, state_.preset};
    if (action == AutoAction::Interval) {
        plan_ = plan;
        state_.total = plan.shots;
        if (!plan.seconds || plan.seconds > automationLimit || !plan.shots || plan.shots > automationLimit ||
            !session_.supportsMedia()) {
            finish(AutoResult::Failed, Unsupported);
            return;
        }
        slot_ = 1;
        referenceSlot_ = 0;
        step_ = Baseline;
        baseline_.emplace().begin();
    } else {
        presetIndex_ = 0;
        presetRead_ = waitingForValue_ = false;
        if (action == AutoAction::ApplyPreset && state_.preset) state_.total = state_.preset->count;
    }
}
void Automation::stop() {
    if (state_.active()) finish(AutoResult::Stopped);
}
bool Automation::finish(AutoResult result, int error) {
    state_.result = result;
    state_.error = error;
    baseline_.reset();
    cards_ = {};
    page_ = {};
    return true;
}
bool Automation::tick(Permit allow, void* context) {
    if (!state_.active()) return false;
    if (!allow(context)) return finish(AutoResult::Stopped);
    const auto& capture = capture_.state();
    if (capture.locked() || capture.recording != Recording::Idle) return finish(AutoResult::Failed, StateChanged);
    if (capture.pending != CaptureAction::None && !waitingForValue_) return false;
    return state_.action == AutoAction::Interval ? interval(allow, context) : preset(allow, context);
}
void Automation::Card::add(const MediaItem& item) {
    if (item.created < date) return;
    if (item.created > date) {
        date = item.created;
        ids.clear();
    }
    if (std::find(ids.begin(), ids.end(), item.key.id) == ids.end()) ids.push_back(item.key.id);
}
bool Automation::Card::contains(const MediaItem& item) const {
    return item.created < date || (item.created == date &&
           std::find(ids.begin(), ids.end(), item.key.id) != ids.end());
}
int Automation::readCard(bool verify, unsigned& photos) {
    auto& card = cards_[slot_ - 1];
    Bytes reply{};
    // Keep the boundary millisecond in the query so equal-time objects can be distinguished by ID.
    const int rc = session_.mediaPage(slot_, card.date ? card.date - 1 : 0, reply);
    if (rc == 0x2013 || rc == 0x201d) return referenceSlot_ ? StateChanged : 0;
    if (rc) return rc;
    if (!parseMediaPage(reply, slot_, page_) || page_.size() >= mediaPageLimit) return Invalid;
    std::optional<MediaKey> newPhoto;
    for (const auto& item : page_) {
        if (card.contains(item) || !photo(item)) continue;
        if (!item.created) return Invalid;
        if (newPhoto && !(*newPhoto == item.key)) photos = 2;
        else if (!newPhoto) {
            newPhoto = item.key;
            photos = 1;
        }
    }
    if (!verify || photos == 1)
        for (const auto& item : page_) card.add(item);
    return 0;
}
bool Automation::interval(Permit allow, void* context) {
    const auto now = now_();
    switch (step_) {
    case Baseline: {
        baseline_->step(session_, {}, nullptr, nullptr);
        const auto& media = baseline_->state();
        if (media.status == MediaState::ListFailed) return finish(AutoResult::Failed, media.error);
        if (media.status != MediaState::Ready) return false;
        for (const auto& item : *media.items) {
            if (!item.created && photo(item)) return finish(AutoResult::Failed, Invalid);
            cards_[item.key.slot - 1].add(item);
        }
        baseline_.reset();
        step_ = Fire;
        break;
    }
    case BeforeShot:
        if (!due(now, due_)) return false;
        if (unsigned photos = 0; const int rc = readCard(false, photos)) return finish(AutoResult::Failed, rc);
        step_ = Fire;
        break;
    case Fire: {
        if (!canRunInterval(capture_.state())) return finish(AutoResult::Failed, SingleShotRequired);
        struct Guard { Automation& self; Permit allow; void* context; } guard{*this, allow, context};
        const int rc = session_.capture(0, [](void* ptr) {
            auto& g = *static_cast<Guard*>(ptr);
            g.self.shotAt_ = g.self.now_();
            return g.allow(g.context) && g.self.session_.singleShot();
        }, &guard);
        if (!allow(context)) return finish(AutoResult::Stopped);
        if (rc) return finish(AutoResult::Failed,
                             rc == StateChanged && !session_.singleShot() ? SingleShotRequired : rc);
        slot_ = referenceSlot_ ? referenceSlot_ : 1;
        step_ = Verify;
        verifySince_ = now_();
        due_ = verifySince_;
        break;
    }
    case Verify: {
        if (now - verifySince_ >= confirmationTimeout) return finish(AutoResult::Failed, ShotUnconfirmed);
        if (!due(now, due_)) return false;
        unsigned photos = 0;
        const int rc = readCard(true, photos);
        if (!allow(context)) return finish(AutoResult::Stopped);
        if (rc && rc != 0x2019) return finish(AutoResult::Failed, rc);
        if (photos > 1) return finish(AutoResult::Failed, StateChanged);
        if (photos == 1) {
            if (++state_.completed == state_.total) return finish(AutoResult::Done);
            referenceSlot_ = slot_;
            due_ = shotAt_ + plan_.seconds * 1000;
            step_ = BeforeShot;
            return true;
        }
        if (!referenceSlot_ && slot_ == 1 && !rc) { slot_ = 2; break; }
        slot_ = referenceSlot_ ? referenceSlot_ : 1;
        due_ = now_() + 500;
        break;
    }
    }
    return false;
}
bool Automation::preset(Permit allow, void* context) {
    const auto& capture = capture_.state();
    if (!presetRead_) {
        const int rc = capture_.refresh();
        if (!allow(context)) return finish(AutoResult::Stopped);
        if (rc || capture.recording != Recording::Idle)
            return finish(AutoResult::Failed, rc ? rc : StateChanged);
        presetRead_ = true;
        if (state_.action == AutoAction::SavePreset) {
            if (!canSavePreset(capture)) return finish(AutoResult::Failed, StateChanged);
            auto value = std::make_shared<Preset>();
            value->mode = uint32_t(capture.find(Parameter::Mode)->current);
            std::strcpy(value->model, session_.model());
            for (size_t i = 0; i < parameterSpecCount; ++i) {
                const auto& spec = parameterSpecs[i];
                const auto* p = capture.find(spec.id);
                if (p && p->options() && (spec.pages & scope(value->mode)))
                    value->entries[value->count++] = {p->current, p->id, p->type};
            }
            if (!save_(*value)) return finish(AutoResult::Failed, PresetStorage);
            state_.preset = std::move(value);
            return finish(AutoResult::Done);
        }
        if (!state_.preset || !state_.preset->matches(capture)) return finish(AutoResult::Failed, StateChanged);
    }
    const auto& value = *state_.preset;
    if (waitingForValue_) {
        if (capture.resultId == resultId_) return false;
        waitingForValue_ = false;
        if (capture.result) return finish(AutoResult::Failed, capture.result);
        ++state_.completed;
        return true;
    }
    while (presetIndex_ < value.count) {
        const auto& entry = value.entries[presetIndex_++];
        CaptureRequest request{CaptureAction::Set, entry.id, entry.type, entry.value};
        if (!value.matches(capture)) return finish(AutoResult::Failed, StateChanged);
        if (!capture.canRequest(request)) continue;
        struct Guard { const Preset& value; Permit allow; void* context; } guard{value, allow, context};
        resultId_ = capture.resultId;
        capture_.execute(request, [](const CaptureState& state, void* ptr) {
            auto& g = *static_cast<Guard*>(ptr);
            return g.allow(g.context) && g.value.matches(state);
        }, &guard);
        waitingForValue_ = true;
        return false;
    }
    const int rc = capture_.refresh();
    if (!allow(context)) return finish(AutoResult::Stopped);
    if (rc || capture.recording != Recording::Idle || !value.matches(capture))
        return finish(AutoResult::Failed, rc ? rc : StateChanged);
    state_.completed = 0;
    for (unsigned i = 0; i < value.count; ++i) {
        const auto& entry = value.entries[i];
        const auto* p = capture.find(entry.id);
        if (p && p->type == entry.type && p->current == entry.value) ++state_.completed;
    }
    return finish(AutoResult::Done);
}
}
