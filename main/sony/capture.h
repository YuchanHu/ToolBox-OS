#pragma once
#include "session.h"
#include <memory>
#include <string>
#include <vector>

namespace sony {
enum class Parameter : uint16_t {
    Shutter = 0xd20d, Aperture = 0x5007, Iso = 0xd21e, Ev = 0x5010, WhiteBalance = 0x5005,
    Focus = 0x500a, FocusArea = 0xd22c, Drive = 0x5013, PhotoFormat = 0xd253, JpegQuality = 0xd252,
    StillEncoding = 0xd287, RawCompression = 0xd288, Stabilization = 0xd0d9,
    MovieStabilization = 0xd0da, MovieFormat = 0xd241, MovieRate = 0xd286, MovieSetting = 0xd242,
    Mode = 0x500e, Battery = 0xd218
};
struct ParameterSpec { Parameter id; const char* name; unsigned pages; };
constexpr unsigned photoPage = 1, moviePage = 2;
extern const ParameterSpec parameterSpecs[];
extern const size_t parameterSpecCount;

struct CameraParameter {
    Parameter id{};
    uint16_t type = 0;
    int64_t current = 0, minimum = 0, maximum = 0, step = 0;
    std::vector<int64_t> allowed;
    size_t options() const;
    int64_t option(size_t index) const;
    size_t indexOf(int64_t value) const;
    bool operator==(const CameraParameter& other) const;
};
using Parameters = std::vector<CameraParameter>;
void collectParameter(const Property&, void*);
std::string parameterText(Parameter, int64_t value);
const char* parameterName(Parameter);

enum class Recording { Unknown, Idle, Active };
enum class CaptureAction { None, Photo, Start, Stop, Set };
struct CaptureRequest {
    CaptureAction action;
    Parameter parameter{};
    uint16_t type = 0;
    int64_t value = 0;
};
struct CaptureState {
    std::shared_ptr<const Parameters> parameters;
    Recording recording = Recording::Unknown;
    CaptureAction pending = CaptureAction::None;
    bool canPhoto = false, canMovie = false, uncertain = false, localStart = false;
    uint32_t since = 0;
    unsigned resultId = 0;
    int result = 0;
    const CameraParameter* find(Parameter id) const;
    bool locked() const;
    bool canRequest(CaptureRequest request) const;
    uint32_t elapsed(uint32_t now) const;
    bool operator==(const CaptureState& other) const;
};

// Owned by the connection worker. At most one user command waits for readback.
class CaptureControl {
public:
    using Clock = uint32_t (*)();
    CaptureControl(Session&, Parameters, Clock);
    const CaptureState& state() const { return state_; }
    using SetPermit = bool (*)(const CaptureState&, void*);
    void execute(CaptureRequest, SetPermit = nullptr, void* context = nullptr);
    int refresh();
    bool tick(bool refreshRequested, void (*notify)(const Event&, void*) = nullptr, void* context = nullptr);
private:
    Session& session_;
    Clock now_;
    CaptureState state_;
    CaptureRequest request_{CaptureAction::None};
    uint32_t lastFull_, lastState_, sentAt_ = 0, waitingSince_ = 0, eventAt_ = 0;
    bool singleState_ = true, eventDirty_ = false, stateDirty_ = false, sent_ = false;
    void observe();
    void finish(int result);
};
}
