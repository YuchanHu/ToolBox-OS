#pragma once
#include "session.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace sony {
struct MediaKey {
    uint32_t id = 0;
    uint8_t slot = 0;
    bool operator==(const MediaKey& other) const { return slot == other.slot && id == other.id; }
};
struct MediaFile {
    uint16_t id = 0;
    uint32_t format = 0;
    uint64_t size = 0;
    std::array<uint8_t, 32> umid{};
    std::string path;
    bool video = false;
    const char* name() const;
    std::string type() const;
    bool operator==(const MediaFile&) const;
};
struct MediaItem {
    MediaKey key;
    uint32_t type = 0;
    uint64_t created = 0, local = 0;
    bool protectedItem = false, dummy = false;
    std::vector<MediaFile> files;
    bool video() const;
    bool sameObject(const MediaItem&) const;
};
using MediaList = std::vector<MediaItem>;
constexpr size_t mediaPageLimit = 100;
const MediaItem* findMedia(const MediaList&, MediaKey);
bool parseMediaPage(Bytes, uint8_t slot, MediaList&);
std::string mediaDate(const MediaItem&);
std::string mediaAge(const MediaItem&, int64_t referenceMs);

struct MediaImage {
    uint16_t width = 0, height = 0;
    std::shared_ptr<uint16_t> pixels;
};
MediaImage decodeThumbnail(Bytes);
struct MediaTile {
    MediaKey key;
    MediaImage image;
};
struct MediaState {
    enum Status { Closed, Loading, Ready, ListFailed, Checking, Deleting, Deleted, DeleteFailed, Unconfirmed };
    Status status = Closed;
    int error = 0;
    std::shared_ptr<const MediaList> items;
    std::array<MediaTile, 4> tiles{};
    std::array<MediaKey, 4> pendingDeletes{};
    Status deleteResult = Closed;
    int deleteError = 0;
    unsigned deleteRevision = 0;
};

using MediaWindow = std::array<MediaKey, 4>;
// One camera transaction per step. Capture and event processing can run between steps.
class MediaWorker {
public:
    const MediaState& state() const { return state_; }
    void begin();
    void invalidate(bool keepPreview = false);
    bool event(const Event&);
    void erase(std::shared_ptr<const MediaItem> item);
    bool deleting() const { return bool(target_); }
    bool hasWork(const MediaWindow&) const;
    bool step(Session&, const MediaWindow&, bool (*allowDelete)(void*), void*);
private:
    enum Scan { Browse, Verify, Confirm, None };
    void scan(Scan);
    bool scanStep(Session&);
    bool thumbnail(Session&, const MediaWindow&);
    MediaState state_;
    Scan scan_ = None;
    uint8_t slot_ = 1;
    bool dates_ = true;
    bool allowErase_ = false;
    bool targetSlotMissing_ = false, removedEvent_ = false;
    uint64_t cursor_ = 0;
    MediaList list_;
    MediaList page_;
    std::shared_ptr<const MediaItem> target_;
};
}
