#include "hearth_sdclip.h"

namespace hearth {

static const char* kBoundary = "--hearthframe";

bool SdClip::begin(fs::FS& fs, const char* dir) {
    fs_ = &fs;
    strncpy(dir_, dir, sizeof(dir_) - 1);
    if (!fs_->exists(dir_)) fs_->mkdir(dir_);
    ready_ = fs_->exists(dir_);
    return ready_;
}

String SdClip::beginClip(long ts) {
    if (!ready_ || clip_) return "";
    clipName_ = String(ts) + ".mjpeg";
    String path = String(dir_) + "/" + clipName_;
    clip_ = fs_->open(path, FILE_WRITE);
    if (!clip_) { clipName_ = ""; return ""; }
    firstFrame_ = true;
    return clipName_;
}

bool SdClip::addFrame(const uint8_t* jpeg, size_t len) {
    if (!clip_) return false;
    // MJPEG multipart part: boundary + content-type/length + JPEG bytes.
    clip_.printf("\r\n%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                 kBoundary, (unsigned)len);
    size_t w = clip_.write(jpeg, len);
    firstFrame_ = false;
    return w == len;
}

void SdClip::endClip() {
    if (!clip_) return;
    clip_.close();
    clipName_ = "";
    prune();
}

String SdClip::clipUrl(const String& httpBase, const String& file) {
    return httpBase + "/clips/" + file;
}

String SdClip::listJson() {
    String out = "[";
    if (!ready_) return out + "]";
    File d = fs_->open(dir_);
    bool first = true;
    File f = d.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String name = String(f.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            // filename stem is the unix ts.
            long ts = atol(name.c_str());
            if (!first) out += ",";
            first = false;
            out += "{\"file\":\"" + name + "\",\"ts\":" + String(ts) +
                   ",\"size\":" + String((long)f.size()) + "}";
        }
        f = d.openNextFile();
    }
    out += "]";
    return out;
}

void SdClip::prune() {
    if (!ready_) return;
    // Collect (ts, name). Single pass; bounded by maxClips_ on the card anyway.
    struct E { long ts; String name; };
    // Two-pass age prune first (cheap: delete anything older than cutoff).
    long nowTs = time(nullptr);
    long cutoff = (nowTs > 100000) ? nowTs - (long)retentionDays_ * 86400L : 0;

    File d = fs_->open(dir_);
    int count = 0;
    long oldestTs = 0; String oldestName;
    File f = d.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            String name = String(f.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            long ts = atol(name.c_str());
            bool deleted = false;
            if (cutoff && ts && ts < cutoff) {
                fs_->remove(String(dir_) + "/" + name);
                deleted = true;
            }
            if (!deleted) {
                count++;
                if (oldestTs == 0 || (ts && ts < oldestTs)) { oldestTs = ts; oldestName = name; }
            }
        }
        f = d.openNextFile();
    }
    // Count cap: drop the single oldest if over budget (called per-clip so one
    // eviction per recording keeps it bounded without a full sort).
    if (count > maxClips_ && oldestName.length())
        fs_->remove(String(dir_) + "/" + oldestName);
}

} // namespace hearth
