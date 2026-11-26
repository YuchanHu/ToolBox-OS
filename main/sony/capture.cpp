#include "capture.h"
#include <algorithm>
#include <cstdio>
#include <tuple>

namespace sony {
const ParameterSpec parameterSpecs[] = {
    {Parameter::Shutter, "快门", 3}, {Parameter::Aperture, "光圈", 3},
    {Parameter::Iso, "ISO", 3}, {Parameter::Ev, "曝光补偿", 3},
    {Parameter::WhiteBalance, "白平衡", 3}, {Parameter::Focus, "对焦模式", 3},
    {Parameter::FocusArea, "对焦区域", 3}, {Parameter::Drive, "驱动模式", photoPage},
    {Parameter::PhotoFormat, "照片格式", photoPage}, {Parameter::StillEncoding, "JPEG/HEIF", photoPage},
    {Parameter::JpegQuality, "图像质量", photoPage}, {Parameter::RawCompression, "RAW 压缩", photoPage},
    {Parameter::MovieFormat, "录像格式", moviePage}, {Parameter::MovieRate, "录制帧率", moviePage},
    {Parameter::MovieSetting, "录制设置", moviePage},
    {Parameter::Stabilization, "防抖", photoPage}, {Parameter::MovieStabilization, "防抖", moviePage},
    {Parameter::Mode, "机身模式", 0},
    {Parameter::Battery, "相机电量", 0},
};
const size_t parameterSpecCount = sizeof(parameterSpecs) / sizeof(*parameterSpecs);

const char* parameterName(Parameter id) {
    for (const auto& spec : parameterSpecs) if (spec.id == id) return spec.name;
    return "";
}
std::string parameterText(Parameter id, int64_t value) {
    char text[40];
    switch (id) {
    case Parameter::Shutter: {
        const unsigned n = uint32_t(value) >> 16, d = value & 0xffff;
        if (value == 0) return "BULB";
        if (!d || value == 0xffffffff) return "-";
        if (n == 1 && d > 1) std::snprintf(text, sizeof text, "1/%u", d);
        else if (n < d && n) std::snprintf(text, sizeof text, "1/%.1f", double(d) / n);
        else std::snprintf(text, sizeof text, "%.2g s", double(n) / d);
        return text;
    }
    case Parameter::Aperture:
        if (!value || value >= 0xfffd) return "-";
        std::snprintf(text, sizeof text, "F%.1f", value / 100.0);
        return text;
    case Parameter::Iso:
        if ((value & 0xffffff) == 0xffffff) return "AUTO";
        return std::to_string(value & 0xffffff);
    case Parameter::Ev:
        std::snprintf(text, sizeof text, "%+.1f EV", value / 1000.0);
        return text;
    case Parameter::Battery:
        return value >= 0 && value <= 100 ? std::to_string(value) + "%" : "-";
    case Parameter::Mode:
        switch (value & 0xffff) {
        case 1: return "M";
        case 2: return "P";
        case 3: return "A";
        case 4: return "S";
        case 0x8000: case 0x8001: return "AUTO";
        case 0x8050: return "录像 P";
        case 0x8051: return "录像 A";
        case 0x8052: return "录像 S";
        case 0x8053: return "录像 M";
        case 0x8054: return "录像场景";
        }
        break;
    case Parameter::WhiteBalance:
        // Sony PTP wire values differ from the Camera Remote SDK enum.
        switch (value) {
        case 1: return "手动";
        case 2: return "AWB";
        case 3: return "单次自动";
        case 4: return "日光";
        case 5: return "荧光灯";
        case 6: return "白炽灯";
        case 7: return "闪光灯";
        case 0x8001: return "荧光灯 暖白色";
        case 0x8002: return "荧光灯 冷白色";
        case 0x8003: return "荧光灯 日光白";
        case 0x8004: return "荧光灯 日光";
        case 0x8010: return "阴天";
        case 0x8011: return "阴影";
        case 0x8012: return "色温";
        case 0x8020: return "自定义 1";
        case 0x8021: return "自定义 2";
        case 0x8022: return "自定义 3";
        case 0x8023: return "自定义";
        case 0x8030: return "水中自动";
        default: return "白平衡选项 " + std::to_string(value);
        }
    case Parameter::Focus:
        switch (value) {
        case 1: return "MF";
        case 2: return "AF-S";
        case 0x8004: return "AF-C";
        case 0x8005: return "AF-A";
        case 0x8006: return "DMF";
        }
        break;
    case Parameter::FocusArea:
        switch (value) {
        case 1: return "广域";
        case 2: return "区";
        case 3: return "中央固定";
        case 0x101: return "点 S";
        case 0x102: return "点 M";
        case 0x103: return "点 L";
        case 0x104: return "扩展点";
        }
        break;
    case Parameter::Drive:
        switch (value) {
        case 1: return "单张";
        case 0x10002: return "连拍 Hi";
        case 0x18010: return "连拍 Hi+";
        case 0x18012: return "连拍 Lo";
        case 0x18013: return "连拍";
        case 0x18015: return "连拍 Mid";
        case 0x38003: return "自拍 5 s";
        case 0x38004: return "自拍 10 s";
        case 0x38005: return "自拍 2 s";
        }
        break;
    case Parameter::PhotoFormat:
        if (value == 1) return "RAW";
        if (value == 2) return "RAW+JPEG";
        if (value == 3) return "JPEG";
        if (value == 4) return "RAW+HEIF";
        if (value == 5) return "HEIF";
        break;
    case Parameter::StillEncoding:
        if (value == 1) return "JPEG";
        if (value == 2) return "HEIF 4:2:2";
        if (value == 3) return "HEIF 4:2:0";
        break;
    case Parameter::JpegQuality: {
        const char* names[] = {"-", "X.Fine", "Fine", "Std", "Light"};
        if (value >= 0 && value < 5) return names[value];
        break;
    }
    case Parameter::RawCompression:
        switch (value) {
        case 1: return "已压缩";
        case 6: return "无损压缩";
        case 7: return "已压缩 (HQ)";
        default: return "RAW 选项 " + std::to_string(value);
        }
    case Parameter::MovieFormat:
        // SDIO wire values from the FX30 v300 captures and the α6700 dataset.
        switch (value) {
        case 0x08: return "XAVC S 4K";
        case 0x09: return "XAVC S HD";
        case 0x0b: return "XAVC HS 4K";
        case 0x0e: return "XAVC S-I 4K";
        case 0x0f: return "XAVC S-I HD";
        case 0x14: return "XAVC S-I DCI 4K";
        }
        break;
    case Parameter::MovieRate: {
        const char* names[] = {"-", "120p", "100p", "60p", "50p", "30p", "25p", "24p", "23.98p", "29.97p", "59.94p"};
        if (value >= 0 && value < 11) return names[value];
        if (value == 0x16) return "24.00p";
        if (value == 0x17) return "119.88p";
        break;
    }
    case Parameter::MovieSetting: {
        struct Setting { unsigned mbps, chroma, depth; };
        const Setting settings[] = {
            {600,422,10}, {500,422,10}, {400,420,10}, {300,422,10}, {280,422,10}, {250,422,10},
            {240,422,10}, {222,422,10}, {200,422,10}, {200,420,10}, {200,420,8}, {185,422,10},
            {150,420,10}, {150,420,8}, {140,422,10}, {111,422,10}, {100,422,10}, {100,420,10},
            {100,420,8}, {93,422,10}, {89,422,10}, {75,420,10}, {60,420,8}, {50,422,10},
            {50,420,10}, {50,420,8}, {45,420,10}, {30,420,10}, {25,420,8}, {16,420,8},
            {520,422,10}, {260,422,10},
        };
        if (value >= 0x26 && value <= 0x45) {
            const auto& s = settings[value - 0x26];
            std::snprintf(text, sizeof text, "%uM %s %ubit", s.mbps, s.chroma == 422 ? "4:2:2" : "4:2:0", s.depth);
            return text;
        }
        if (!value) return "-";
        break;
    }
    case Parameter::Stabilization:
    case Parameter::MovieStabilization:
        if (value == 1) return "关闭";
        if (value == 2) return id == Parameter::Stabilization ? "开启" : "标准";
        if (id == Parameter::MovieStabilization && value == 3) return "增强";
        if (id == Parameter::MovieStabilization && value == 4) return "动态增强";
        break;
    }
    std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(value));
    return text;
}
namespace {
int64_t scalar(Bytes b, uint16_t type) {
    int64_t value = 0;
    for (size_t i = 0; i < b.size; ++i) value |= int64_t(b.data[i]) << (8 * i);
    const int64_t sign = int64_t(1) << (8 * b.size - 1);
    return (type & 1) && (value & sign) ? value - 2 * sign : value;
}
Recording recording(int raw) {
    if (raw == 0) return Recording::Idle;
    if (raw == 1) return Recording::Active;
    return Recording::Unknown;
}
bool movieAction(CaptureAction action) {
    return action == CaptureAction::Start || action == CaptureAction::Stop;
}
}
void collectParameter(const Property& p, void* context) {
    if (!p.enabled || p.type < 1 || p.type > 6) return;
    const auto id = static_cast<Parameter>(p.code);
    if (!*parameterName(id)) return;
    CameraParameter value;
    value.id = id;
    value.type = p.type;
    value.current = scalar(p.current, p.type);
    if (p.writable == 1 && p.enabled == 1 && id != Parameter::Mode && id != Parameter::Battery) {
        size_t width = p.current.size;
        if (p.form == 1) {
            value.minimum = scalar({p.constraints.data, width}, p.type);
            value.maximum = scalar({p.constraints.data + width, width}, p.type);
            value.step = scalar({p.constraints.data + 2 * width, width}, p.type);
        } else if (p.form == 2) {
            const auto* bytes = p.constraints.data;
            const size_t count = bytes[0] | (size_t(bytes[1]) << 8);
            value.allowed.reserve(count);
            for (size_t i = 0; i < count; ++i)
                value.allowed.push_back(scalar({bytes + 2 + i * width, width}, p.type));
        }
    }
    static_cast<Parameters*>(context)->push_back(std::move(value));
}
size_t CameraParameter::options() const {
    if (step <= 0 || maximum < minimum) return allowed.size();
    const auto count = uint64_t((maximum - minimum) / step) + 1;
    return size_t(std::min<uint64_t>(count, SIZE_MAX));
}
int64_t CameraParameter::option(size_t index) const {
    return allowed.empty() ? minimum + int64_t(index) * step : allowed[index];
}
size_t CameraParameter::indexOf(int64_t value) const {
    if (!allowed.empty()) return size_t(std::find(allowed.begin(), allowed.end(), value) - allowed.begin());
    if (step > 0 && value >= minimum && value <= maximum && (value - minimum) % step == 0)
        return size_t((value - minimum) / step);
    return options();
}
bool CameraParameter::operator==(const CameraParameter& other) const {
    return std::tie(id, type, current, minimum, maximum, step, allowed) ==
           std::tie(other.id, other.type, other.current, other.minimum, other.maximum, other.step, other.allowed);
}
const CameraParameter* CaptureState::find(Parameter id) const {
    if (parameters) for (const auto& p : *parameters) if (p.id == id) return &p;
    return nullptr;
}
bool CaptureState::locked() const {
    return recording == Recording::Active || uncertain || movieAction(pending);
}
bool CaptureState::canRequest(CaptureRequest request) const {
    if (pending != CaptureAction::None) return false;
    if (request.action == CaptureAction::Stop) return canMovie && recording == Recording::Active;
    if (recording != Recording::Idle || locked()) return false;
    switch (request.action) {
    case CaptureAction::Photo: return canPhoto;
    case CaptureAction::Start: return canMovie;
    case CaptureAction::Set: {
        auto* p = find(request.parameter);
        return p && p->type == request.type && p->indexOf(request.value) < p->options();
    }
    default: return false;
    }
}
uint32_t CaptureState::elapsed(uint32_t now) const {
    const auto ms = now - since;
    return (ms > (localStart ? 500u : 0u) ? ms - (localStart ? 500u : 0u) : 0u) / 1000;
}
bool CaptureState::operator==(const CaptureState& other) const {
    return std::tie(parameters, recording, pending, canPhoto, canMovie, uncertain, localStart, since, resultId, result) ==
           std::tie(other.parameters, other.recording, other.pending, other.canPhoto, other.canMovie, other.uncertain,
                    other.localStart, other.since, other.resultId, other.result);
}
CaptureControl::CaptureControl(Session& session, Parameters initial, Clock clock)
    : session_(session), now_(clock), lastFull_(now_()), lastState_(lastFull_) {
    state_.parameters = std::make_shared<Parameters>(std::move(initial));
    state_.canPhoto = session_.supportsCapture();
    state_.canMovie = session_.supportsMovie();
    observe();
}
void CaptureControl::observe() {
    const auto next = recording(session_.recordingRaw());
    if (next == Recording::Active && state_.recording != next) {
        state_.localStart = sent_ && request_.action == CaptureAction::Start;
        state_.since = state_.localStart ? sentAt_ : now_();
    }
    if (next == Recording::Unknown) state_.uncertain = session_.recordingRaw() >= 2 || state_.locked();
    else state_.uncertain = false;
    state_.recording = next;
}
int CaptureControl::refresh() {
    Parameters next;
    const int result = session_.refresh(collectParameter, &next);
    lastFull_ = lastState_ = now_();
    if (!result) {
        if (next != *state_.parameters) state_.parameters = std::make_shared<Parameters>(std::move(next));
        eventDirty_ = stateDirty_ = false;
    } else if (state_.parameters->size()) {
        state_.parameters = std::make_shared<Parameters>();
    }
    observe();
    return result;
}
void CaptureControl::finish(int result) {
    request_ = {CaptureAction::None};
    state_.pending = CaptureAction::None;
    state_.result = result;
    ++state_.resultId;
    sent_ = false;
}
void CaptureControl::execute(CaptureRequest request, SetPermit permit, void* context) {
    if (!state_.canRequest(request)) { finish(StateChanged); return; }
    request_ = request;
    state_.result = 0;
    const auto action = request.action;
    if (action == CaptureAction::Photo) {
        const int result = session_.capture(0);
        observe();
        finish(result);
        return;
    }
    int result;
    if (movieAction(action)) {
        state_.pending = action;
        result = session_.movie(action == CaptureAction::Start ? 0 : 1, [](void* context) {
            auto& self = *static_cast<CaptureControl*>(context);
            self.sentAt_ = self.now_();
            self.sent_ = true;
        }, this);
        if (result && session_.connected()) sent_ = false;
        if (result || action == CaptureAction::Stop) observe();
    } else {
        result = refresh();
        if (!result && permit && !permit(state_, context)) { finish(StateChanged); return; }
        if (!result && !state_.canRequest(request)) result = StateChanged;
        if (!result && state_.find(request.parameter)->current == request.value) { finish(0); return; }
        if (!result) result = session_.setScalar(uint16_t(request.parameter), request.type, request.value);
    }
    if (result) {
        if (session_.connected()) refresh();
        finish(result);
        return;
    }
    state_.pending = action;
    waitingSince_ = now_();
}
bool CaptureControl::tick(bool refreshRequested, void (*notify)(const Event&, void*), void* context) {
    Event event{};
    for (int i = 0; i < 8; ++i) {
        const auto result = session_.event(event);
        if (result < 0) return false;
        if (!result) break;
        if (notify) notify(event, context);
        if (event.code == 0xc234 || (event.code == 0xc203 && (!event.count || event.params[0] == recordingProperty))) {
            stateDirty_ = true;
        } else if (event.code == 0xc203 && *parameterName(static_cast<Parameter>(event.params[0]))) {
            if (!eventDirty_) eventAt_ = now_();
            eventDirty_ = true;
        }
    }
    const auto now = now_();
    const bool pendingMovie = movieAction(state_.pending);
    const bool pendingSet = state_.pending == CaptureAction::Set;
    const bool confirmTimedOut = state_.pending != CaptureAction::None && now - waitingSince_ >= 3500;
    if (refreshRequested || now - lastFull_ >= 8000 ||
        (eventDirty_ && now - eventAt_ >= 80 && now - lastFull_ >= 300) ||
        (pendingSet && now - lastFull_ >= 550) ||
        (!singleState_ && ((stateDirty_ && now - lastFull_ >= 300) ||
                          now - lastState_ >= (pendingMovie ? 550u : 2000u)))) {
        refresh();
    } else if (singleState_ && state_.canMovie &&
               ((stateDirty_ && now - lastState_ >= 150) || now - lastState_ >= (pendingMovie ? 250u : 1000u))) {
        const int result = session_.readRecording();
        lastState_ = now_();
        stateDirty_ = false;
        if (!result) {
            const auto previous = state_.recording;
            observe();
            if (previous != state_.recording && state_.recording != Recording::Unknown) {
                eventDirty_ = true;
                eventAt_ = now;
            }
        } else if (result == 0x2005 || result == 0x2006 || result == 0x201d || result == Unsupported) {
            singleState_ = false;
            if (session_.connected()) refresh();
        } else if (now - lastFull_ >= 2000 && session_.connected()) refresh();
    }
    if (!session_.connected()) return false;
    if ((state_.pending == CaptureAction::Start && state_.recording == Recording::Active) ||
        (state_.pending == CaptureAction::Stop && state_.recording == Recording::Idle)) {
        eventDirty_ = true;  // capture the format and exposure of the confirmed mode
        eventAt_ = now;
        finish(0);
    } else if (pendingSet) {
        auto* p = state_.find(request_.parameter);
        if (p && p->type == request_.type && p->current == request_.value) finish(0);
        else if (confirmTimedOut) finish(StateChanged);
    } else if (pendingMovie && confirmTimedOut) {
        refresh();
        if (!session_.connected()) return false;
        const auto target = request_.action == CaptureAction::Start ? Recording::Active : Recording::Idle;
        finish(state_.recording == target ? 0 : StateChanged);
    }
    return true;
}
}
