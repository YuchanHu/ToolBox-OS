// Sony v300 media wire layouts follow alpha2go (Apache-2.0), see licenses/.
#include "media.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <tuple>

namespace sony {
namespace {
constexpr size_t maxItems = 8192;
constexpr uint64_t minDate = 946684800000; // 2000-01-01, also rejects an unset device RTC.
constexpr uint64_t maxDate = 4102444800000; // 2100-01-01
bool validDate(uint64_t ms) { return ms >= minDate && ms < maxDate; }
struct Reader {
    Bytes rest;
    bool ok = true;
    Bytes take(size_t n) {
        if (n > rest.size) { ok = false; return {}; }
        Bytes value{rest.data, n};
        rest = {rest.data + n, rest.size - n};
        return value;
    }
    uint64_t number(size_t n) {
        const auto value = take(n);
        uint64_t v = 0;
        for (size_t i = 0; i < value.size; ++i) v |= uint64_t(value.data[i]) << (8 * i);
        return v;
    }
};
}

int Session::mediaDates(uint8_t slot, bool& hasDates) {
    hasDates = false;
    const int rc = transact(0x923b, {slot});
    if (rc) return rc;
    Reader r{{data_, size_}};
    const auto count = r.number(4);
    if (!r.ok || count * 8 != r.rest.size) return Invalid;
    hasDates = count != 0;
    return 0;
}
int Session::mediaPage(uint8_t slot, uint64_t after, Bytes& reply) {
    const int rc = transact(0x923c, {uint32_t(after), uint32_t(after >> 32), mediaPageLimit, slot, 0});
    reply = rc ? Bytes{} : Bytes{data_, size_};
    return rc;
}
int Session::mediaThumbnail(uint8_t slot, uint32_t id, uint16_t file, Bytes& reply) {
    reply = {};
    if (!supportsThumbnail()) return Unsupported;
    const int rc = transact(0x923e, {id, uint32_t(slot) << 24 | file, 1});
    reply = rc ? Bytes{} : Bytes{data_, size_};
    return rc;
}
int Session::deleteMedia(uint8_t slot, uint32_t id) {
    return supportsMediaDelete() ? transact(0x9250, {id, slot}) : Unsupported;
}

const char* MediaFile::name() const {
    const auto sep = path.find_last_of("/\\");
    return path.c_str() + (sep == std::string::npos ? 0 : sep + 1);
}
std::string MediaFile::type() const {
    const auto dot = path.find_last_of('.');
    auto ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (auto& c : ext) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (ext == "JPG") return "JPEG";
    if (ext == "ARW") return "RAW";
    if (ext == "HIF" || ext == "HEIC") return "HEIF";
    return ext;
}
bool MediaFile::operator==(const MediaFile& other) const {
    return id == other.id && format == other.format && size == other.size && umid == other.umid &&
           path == other.path && video == other.video;
}
bool MediaItem::video() const {
    return type == 4 || std::any_of(files.begin(), files.end(), [](const MediaFile& f) { return f.video; });
}
bool MediaItem::sameObject(const MediaItem& other) const {
    return key == other.key && type == other.type && created == other.created && local == other.local &&
           protectedItem == other.protectedItem && dummy == other.dummy && files == other.files;
}
const MediaItem* findMedia(const MediaList& list, MediaKey key) {
    const auto i = std::find_if(list.begin(), list.end(), [&](const MediaItem& item) { return item.key == key; });
    return i == list.end() ? nullptr : &*i;
}

bool parseMediaPage(Bytes data, uint8_t slot, MediaList& out) {
    out.clear();
    Reader r{data};
    if (r.number(4) != 100) return false;
    r.take(0x8c);
    const auto count = r.number(4);
    if (count > mediaPageLimit) return false;
    for (size_t i = 0; i < count && r.ok; ++i) {
        MediaItem item;
        item.type = r.number(4);
        item.key = {uint32_t(r.number(4)), slot};
        r.take(20); // directory, file number, group type/ID and representative flag
        item.created = r.number(8);
        r.take(8); // modified UTC
        item.local = r.number(8);
        r.take(12); // modified local and rating
        item.protectedItem = r.number(4) != 0;
        item.dummy = r.number(4) != 0;
        r.take(r.number(4)); // shot marks
        const auto files = r.number(4);
        if (files > r.rest.size / 64) return false;
        for (size_t j = 0; j < files && r.ok; ++j) {
            MediaFile file;
            file.id = uint16_t(r.number(4)); // u16 in a 4-byte field
            auto path = r.take(r.number(4));
            while (path.size && path.data[path.size - 1] == 0) --path.size;
            if (!path.size) return false;
            file.path.assign(reinterpret_cast<const char*>(path.data), path.size);
            if (!*file.name() || file.path.find('\0') != std::string::npos) return false;
            file.format = r.number(4);
            file.size = r.number(8);
            auto umid = r.take(file.umid.size());
            if (umid.size) std::copy_n(umid.data, umid.size, file.umid.begin());
            if (r.number(4)) r.take(8); // image size
            file.video = r.number(4) != 0;
            if (file.video) r.take(0x4c);
            if (r.number(4)) r.take(0x10); // audio info
            file.video = file.video || file.format == 0xb982 || file.type() == "MP4";
            item.files.push_back(std::move(file));
        }
        // Prefer the JPEG of a pair for display. Preserve every file for the deletion scope.
        std::sort(item.files.begin(), item.files.end(), [](const MediaFile& a, const MediaFile& b) {
            return std::make_tuple(!a.video, a.format != 0x3801, a.id) <
                   std::make_tuple(!b.video, b.format != 0x3801, b.id);
        });
        out.push_back(std::move(item));
    }
    return r.ok;
}

std::string mediaDate(const MediaItem& item) {
    const bool utcOnly = !validDate(item.local);
    const auto timestamp = utcOnly ? item.created : item.local;
    if (!validDate(timestamp)) return {};
    const time_t seconds = timestamp / 1000;
    tm wall{};
#ifdef _WIN32
    if (gmtime_s(&wall, &seconds)) return {};
#else
    if (!gmtime_r(&seconds, &wall)) return {};
#endif
    char date[24];
    std::strftime(date, sizeof date, "%Y/%m/%d %H:%M:%S", &wall);
    return utcOnly ? std::string(date) + " UTC" : date;
}
std::string mediaAge(const MediaItem& item, int64_t referenceMs) {
    if (!validDate(item.created) || referenceMs < 0 ||
        !validDate(uint64_t(referenceMs)) || uint64_t(referenceMs) < item.created) return {};
    const auto seconds = (referenceMs - item.created) / 1000;
    const char* suffix = item.video() ? "前创建" : "前拍摄";
    if (seconds < 60) return std::to_string(seconds) + "秒" + suffix;
    if (seconds < 3600) return std::to_string(seconds / 60) + "分钟" + suffix;
    if (seconds < 86400) return std::to_string(seconds / 3600) + "小时" + suffix;
    return std::to_string(seconds / 86400) + "天" + suffix;
}

void MediaWorker::scan(Scan mode) {
    list_ = {};
    scan_ = mode;
    slot_ = mode == Verify ? target_->key.slot : 1;
    cursor_ = 0;
    dates_ = true;
    allowErase_ = false;
    targetSlotMissing_ = false;
}
void MediaWorker::begin() {
    state_ = {};
    state_.status = MediaState::Loading;
    scan(Browse);
}
void MediaWorker::invalidate(bool keepPreview) {
    if (target_ && state_.status == MediaState::Deleting) {
        scan(Confirm);
    } else {
        if (target_) { target_.reset(); state_.status = MediaState::DeleteFailed; state_.error = StateChanged; }
        else state_.status = MediaState::Loading;
        if (!keepPreview) {
            state_.tiles = {};
            if (!target_ && state_.status == MediaState::Loading) state_.items.reset();
        }
        scan(Browse);
    }
}
bool MediaWorker::event(const Event& e) {
    if (e.code != 0xc240 && e.code != 0xc201 && e.code != 0xc202 && e.code != 0xc206 &&
        (e.code < 0x4002 || e.code > 0x4005)) return false;
    if (target_ && e.code == 0xc240 && e.count == 3 && state_.status == MediaState::Checking &&
        (e.params[0] != target_->key.slot || e.params[1] != target_->key.id)) return false;
    if (target_ && state_.status == MediaState::Deleting && e.code == 0xc240 &&
        e.count == 3 && e.params[0] == target_->key.slot && e.params[1] == target_->key.id && e.params[2] == 1)
        removedEvent_ = true;
    invalidate(e.code == 0xc201 || e.code == 0xc206 || e.code == 0xc240);
    return true;
}
void MediaWorker::erase(std::shared_ptr<const MediaItem> item) {
    target_ = std::move(item);
    state_.status = MediaState::Checking;
    state_.error = 0;
    removedEvent_ = false;
    scan(Verify);
}
bool MediaWorker::scanStep(Session& session) {
    int rc = 0;
    bool slotDone = false;
    if (dates_) {
        bool present;
        rc = session.mediaDates(slot_, present);
        if (rc == 0x2013 || rc == 0x201d) {
            if (scan_ == Confirm && slot_ == target_->key.slot) targetSlotMissing_ = true;
            rc = 0;
        }
        slotDone = !present;
        dates_ = false;
    } else {
        Bytes reply{};
        rc = session.mediaPage(slot_, cursor_, reply);
        if (!rc && !parseMediaPage(reply, slot_, page_)) rc = Invalid;
        if (!rc && list_.size() + page_.size() > maxItems) rc = Unsupported;
        if (!rc) {
            slotDone = page_.size() < mediaPageLimit;
            const auto next = page_.empty() ? 0 : page_.back().created;
            if (!slotDone && next <= cursor_) rc = Invalid;
            cursor_ = next;
            for (auto& item : page_) list_.push_back(std::move(item));
            page_.clear();
        }
    }
    if (!rc && !slotDone) return false;
    if (!rc && scan_ != Verify && slot_ == 1) {
        ++slot_;
        cursor_ = 0;
        dates_ = true;
        return false;
    }
    const auto mode = scan_;
    scan_ = None;
    if (!rc) {
        std::sort(list_.begin(), list_.end(), [](const MediaItem& a, const MediaItem& b) {
            return std::tie(a.key.slot, a.key.id) < std::tie(b.key.slot, b.key.id);
        });
        // Identical records may repeat at a page boundary. Conflicting records
        // with the same key make the scan unsafe for deleting or confirming.
        auto less = std::adjacent_find(list_.begin(), list_.end(), [](const MediaItem& a, const MediaItem& b) {
            return a.key == b.key && !a.sameObject(b);
        });
        if (less != list_.end()) rc = Invalid;
        else list_.erase(std::unique(list_.begin(), list_.end(), [](const MediaItem& a, const MediaItem& b) {
            return a.key == b.key;
        }), list_.end());
    }
    if (mode == Verify) {
        const auto* actual = rc ? nullptr : findMedia(list_, target_->key);
        allowErase_ = actual && !actual->protectedItem && !actual->dummy && !actual->files.empty() &&
                      actual->sameObject(*target_);
        if (allowErase_) return false;
        state_.status = MediaState::DeleteFailed;
        state_.error = rc ? rc : StateChanged;
    } else if (rc) {
        state_.status = mode == Confirm ? MediaState::Unconfirmed : MediaState::ListFailed;
        state_.error = rc;
        state_.tiles = {};
    } else {
        if (mode == Confirm) {
            const bool remains = findMedia(list_, target_->key) || (targetSlotMissing_ && !removedEvent_);
            state_.status = remains ? MediaState::Unconfirmed : MediaState::Deleted;
        } else {
            state_.status = MediaState::Ready;
            state_.error = 0;
        }
        list_.erase(std::remove_if(list_.begin(), list_.end(), [](const MediaItem& i) {
            return i.dummy || i.files.empty();
        }), list_.end());
        std::sort(list_.begin(), list_.end(), [](const MediaItem& a, const MediaItem& b) {
            const auto dateA = validDate(a.created) ? a.created : 0;
            const auto dateB = validDate(b.created) ? b.created : 0;
            return std::tie(dateA, a.key.slot, a.key.id) > std::tie(dateB, b.key.slot, b.key.id);
        });
        for (auto& tile : state_.tiles) {
            if (!tile.key.slot) continue;
            const auto* old = state_.items ? findMedia(*state_.items, tile.key) : nullptr;
            const auto* actual = findMedia(list_, tile.key);
            if (!old || !actual || !actual->sameObject(*old)) tile = {};
        }
        state_.items = std::make_shared<MediaList>(std::move(list_));
    }
    target_.reset();
    list_ = {};
    if (mode == Verify && state_.error == StateChanged) {
        state_.tiles = {};
        scan(Browse);
    }
    return true;
}
bool MediaWorker::thumbnail(Session& session, const MediaWindow& window) {
    if (!state_.items || state_.status == MediaState::ListFailed || state_.status == MediaState::Unconfirmed)
        return false;
    bool changed = false;
    for (auto& tile : state_.tiles) {
        if (tile.key.slot && std::find(window.begin(), window.end(), tile.key) == window.end()) {
            tile = {};
            changed = true;
        }
    }
    for (const auto key : window) {
        if (!key.slot || std::any_of(state_.tiles.begin(), state_.tiles.end(), [&](const MediaTile& t) {
            return t.key == key;
        })) continue;
        const auto* item = findMedia(*state_.items, key);
        if (!item) continue;
        auto tile = std::find_if(state_.tiles.begin(), state_.tiles.end(), [](const MediaTile& t) { return !t.key.slot; });
        if (tile == state_.tiles.end()) return changed;
        Bytes bytes{};
        tile->key = key;
        if (!session.mediaThumbnail(key.slot, key.id, item->files.front().id, bytes))
            tile->image = decodeThumbnail(bytes);
        return true;
    }
    return changed;
}
bool MediaWorker::step(Session& session, const MediaWindow& window, bool (*allowDelete)(void*), void* context) {
    if (scan_ != None) return scanStep(session);
    if (!allowErase_) return thumbnail(session, window);
    allowErase_ = false;
    if (!allowDelete(context)) {
        state_.status = MediaState::DeleteFailed;
        state_.error = StateChanged;
        target_.reset();
    } else {
        const int rc = session.deleteMedia(target_->key.slot, target_->key.id);
        if (rc) {
            state_.status = rc < 0 ? MediaState::Unconfirmed : MediaState::DeleteFailed;
            state_.error = rc;
            target_.reset();
        } else {
            state_.status = MediaState::Deleting;
            scan(Confirm);
        }
    }
    return true;
}
bool MediaWorker::hasWork(const MediaWindow& window) const {
    if (scan_ != None || allowErase_) return true;
    if (!state_.items || state_.status == MediaState::ListFailed || state_.status == MediaState::Unconfirmed) return false;
    return std::any_of(window.begin(), window.end(), [&](const MediaKey& key) {
        return key.slot && findMedia(*state_.items, key) &&
               std::none_of(state_.tiles.begin(), state_.tiles.end(), [&](const MediaTile& tile) { return tile.key == key; });
    });
}
}
