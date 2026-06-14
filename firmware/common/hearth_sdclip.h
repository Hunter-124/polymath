#pragma once
// hearth_sdclip — motion/person-gated clip recorder on microSD + retention prune.
//
// Clips are JPEG-sequence Motion-JPEG containers named "<unix>.mjpeg" under
// /clips on the SD card. (We avoid an MP4 muxer on the MCU; the hub/app treat the
// clip_url as opaque media. FABRIC.md examples show .mp4 but the field is just a
// URL — see README "FABRIC notes".) Listing + serving is done by hearth_httpd.
//
// Retention: prune clips older than retention_days, oldest-first, also enforcing
// a max clip count so a busy day can't exhaust the card.

#include <Arduino.h>
#include <FS.h>

namespace hearth {

class SdClip {
public:
    // Mounts SD (SD_MMC by default on cam boards; caller may pre-mount and pass
    // the FS). Returns false if no card. dir defaults to "/clips".
    bool begin(fs::FS& fs, const char* dir = "/clips");
    bool ready() const { return ready_; }

    void setRetentionDays(int d) { retentionDays_ = d; }
    void setMaxClips(int n)      { maxClips_ = n; }

    // Open a new clip keyed by `ts` (unix seconds). Returns the bare filename
    // (e.g. "1718246542.mjpeg") or "" on failure. Append frames with addFrame.
    String beginClip(long ts);
    bool   addFrame(const uint8_t* jpeg, size_t len);   // MJPEG part
    void   endClip();
    bool   recording() const { return clip_; }

    // "http://<host>/clips/<file>" — the clip_url for the CameraEvent.
    static String clipUrl(const String& httpBase, const String& file);

    // JSON array body for GET /clips: [{"file","ts","size"}...] newest first.
    String listJson();

    // Serve helpers used by hearth_httpd.
    const char* dir() const { return dir_; }
    fs::FS*     fs()  const { return fs_; }

    // Prune by age + count. Safe to call after each clip.
    void prune();

private:
    fs::FS* fs_ = nullptr;
    char    dir_[24] = "/clips";
    bool    ready_ = false;
    int     retentionDays_ = 14;
    int     maxClips_ = 500;

    File    clip_;          // open while recording
    String  clipName_;
    bool    firstFrame_ = false;
};

} // namespace hearth
