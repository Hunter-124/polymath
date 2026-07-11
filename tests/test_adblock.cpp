// Unit tests for the pure adblock URL classifier (B3).
//
// Exercises polymath::isAdRequest() — the state-free function mirroring
// WebAdblockInterceptor's built-in default rules (host suffix/prefix list +
// YouTube ad path markers + googlevideo ctier=L/oad= stream heuristics).
// Does not construct a WebAdblockInterceptor (no QWebEngine/QApplication
// needed), does not touch data/adblock_extra.txt, and needs no network.
#include "web_adblock_interceptor.h"
#undef NDEBUG   // keep assert() active even in Release (otherwise a no-op)
#include <cassert>
#include <cstdio>

using namespace polymath;

int main() {
    // --- Positive: known ad/tracker hosts + YouTube ad paths -------------
    assert(isAdRequest("https://doubleclick.net/ad"));
    assert(isAdRequest("https://googleads.g.doubleclick.net/pagead/ads"));
    assert(isAdRequest("https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js"));
    assert(isAdRequest("https://static.doubleclick.net/instream/ad_status.js"));
    assert(isAdRequest("https://adservice.google.com/adsid/integrator.js"));
    assert(isAdRequest("https://adservice.google.de/adsid/integrator.js"));   // wildcard TLD
    assert(isAdRequest("https://imasdk.googleapis.com/js/sdkloader/ima3.js"));
    assert(isAdRequest("https://www.youtube.com/pagead/viewthroughconversion"));
    assert(isAdRequest("https://www.youtube.com/ptracking?foo=bar"));
    assert(isAdRequest("https://www.youtube.com/api/stats/ads?ns=yt"));
    assert(isAdRequest("https://www.youtube.com/youtubei/v1/log_event"));
    assert(isAdRequest("https://r4---sn-abcd.googlevideo.com/videoplayback?ctier=L&id=abc"));
    assert(isAdRequest("https://r4---sn-abcd.googlevideo.com/videoplayback?oad=1&id=abc"));

    // --- Negative: normal playback / page traffic must pass through ------
    assert(!isAdRequest("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
    assert(!isAdRequest("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ"));
    assert(!isAdRequest("https://r4---sn-abcd.googlevideo.com/videoplayback?id=abc&itag=137"));
    assert(!isAdRequest("https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg"));
    assert(!isAdRequest("https://www.googleapis.com/youtube/v3/videos"));
    assert(!isAdRequest("https://www.google.com/search?q=cats"));

    std::puts("test_adblock: OK");
    return 0;
}
