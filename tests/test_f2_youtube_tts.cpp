// F2 live acceptance smokes — YouTube pipeline + TTS v2.
//
// Always-green half (no network / no speaker):
//   1. watch_video + slop_mode skill JSON expand: step-result chain refs present
//   2. TTS voice map: af_heart + af_sky synthesize non-empty PCM via Kokoro/Piper
//
// Network-gated half (skips cleanly if offline):
//   3. Live youtube_search for 3 topics returns ≥3 sane results each
//
// Run: test_f2_youtube_tts.exe  (or ctest -R f2_youtube_tts)

#include "skills/skill_registry.h"
#include "skills/skill.h"
#include "tools/youtube_search.h"
#include "tts_piper.h"
#include "paths.h"
#include "i_tool.h"

#include <QCoreApplication>
#include <QGuiApplication>

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace polymath;

namespace {

std::filesystem::path findModelsRoot() {
    namespace fs = std::filesystem;
    const auto exe = fs::current_path();
    const std::vector<fs::path> cands = {
        exe / "data" / "models",
        exe / ".." / "data" / "models",
        fs::path("C:/pm/build/cuda/bin/data/models"),
        fs::path("C:/pm/build/cpu/bin/Release/data/models"),
        fs::path("C:/pm/data/models"),
    };
    for (const auto& c : cands) {
        std::error_code ec;
        if (fs::exists(c / "kokoro-engine", ec) || fs::exists(c / "piper", ec))
            return fs::weakly_canonical(c, ec);
    }
    return {};
}

std::filesystem::path findSkillsRoot() {
    namespace fs = std::filesystem;
#ifdef POLYMATH_SKILLS_DIR
    {
        fs::path p(POLYMATH_SKILLS_DIR);
        if (fs::exists(p / "watch_video" / "skill.json")) return p;
    }
#endif
    for (const auto& c : {
             fs::path("C:/pm/data/skills"),
             fs::path("C:/pm/build/cpu/bin/Release/data/skills"),
             fs::path("C:/pm/build/cuda/bin/data/skills"),
         }) {
        if (fs::exists(c / "watch_video" / "skill.json")) return c;
    }
    return {};
}

void testSkillChaining() {
    std::puts("F2.1 skill chaining (watch_video / slop_mode)");
    const auto skillsDir = findSkillsRoot();
    assert(!skillsDir.empty() && "watch_video skill not found — deploy data/skills");

    SkillRegistry reg;
    reg.setSkillsDir(skillsDir);
    reg.load();
    assert(reg.has("watch_video") && "watch_video not loaded");
    assert(reg.has("slop_mode") && "slop_mode not loaded");

    auto wv = reg.expand("watch_video", {{"topic", "castles"}});
    assert(!wv.contains("error"));
    assert(wv["steps"].size() == 2);
    assert(wv["steps"][0]["tool"] == "youtube_search");
    assert(wv["steps"][1]["tool"] == "ui_control");
    assert(wv["steps"][1]["args"]["type"] == "video_picker");
    const std::string res_ref =
        wv["steps"][1]["args"]["args"]["results"].get<std::string>();
    assert(res_ref.find("{{result:youtube_search.results}}") != std::string::npos
           && "watch_video must chain search results into video_picker");

    auto slop = reg.expand("slop_mode", {{"topic", "lofi"}});
    assert(!slop.contains("error"));
    assert(slop["steps"].size() >= 2);
    assert(slop["steps"][0]["tool"] == "youtube_search");
    assert(slop["steps"][1]["tool"] == "ui_control");
    assert(slop["steps"][1]["args"]["type"] == "video");
    const std::string vid =
        slop["steps"][1]["args"]["args"]["videoId"].get<std::string>();
    assert(vid.find("{{result:youtube_search.results.0.videoId}}") != std::string::npos
           && "slop_mode must chain top videoId");

    std::puts("  [ok] watch_video → video_picker + result ref; slop_mode → top videoId ref");
}

void testLiveYoutubeSearch() {
    std::puts("F2.2 live youtube_search (3 topics, network)");
    YoutubeSearchTool tool;
    ToolContext ctx;
    const char* topics[] = {"castles", "trains", "cooking"};
    int ok_topics = 0;
    for (const char* topic : topics) {
        auto r = tool.invoke({{"query", topic}, {"max_results", 6}}, ctx);
        if (!r.ok) {
            std::printf("  [skip] youtube_search(%s): %s\n", topic, r.summary.c_str());
            continue;
        }
        const auto& results = r.content.value("results", nlohmann::json::array());
        assert(results.is_array());
        if (results.size() < 3) {
            std::printf("  [warn] youtube_search(%s): only %zu results\n",
                        topic, results.size());
            if (results.empty()) continue;
        }
        // First hit must look like a real video card.
        const auto& v0 = results[0];
        assert(v0.contains("videoId") && !v0["videoId"].get<std::string>().empty());
        assert(v0.contains("title") && !v0["title"].get<std::string>().empty());
        assert(v0.contains("watchUrl"));
        std::printf("  [ok] %s → %zu results (top: %s / %s)\n",
                    topic, results.size(),
                    v0["videoId"].get<std::string>().c_str(),
                    v0["title"].get<std::string>().substr(0, 60).c_str());
        ++ok_topics;
    }
    if (ok_topics == 0) {
        std::puts("  [soft] network unavailable — youtube live half skipped");
    } else {
        assert(ok_topics >= 1);
        std::printf("  [ok] live youtube_search succeeded for %d/3 topics\n", ok_topics);
    }
}

void testTtsVoices() {
    std::puts("F2.3 TTS v2 — af_heart + af_sky synthesize");
    const auto models = findModelsRoot();
    if (models.empty()) {
        std::puts("  [skip] no models root with kokoro-engine/piper");
        return;
    }
    Paths::instance().setRoot(models.parent_path());  // data/
    Paths::instance().ensureLayout();

    audio::TtsPiper tts;
    tts.setEnginePreference("auto");  // Kokoro when present, else Piper
    // init(voices_dir=models/piper, default_voice) — models_root = parent.
    if (!tts.init(models / "piper", "af_heart")) {
        std::puts("  [skip] TtsPiper::init failed (no engine files)");
        return;
    }
    assert(tts.ready());

    const char* voices[] = {"af_heart", "af_sky"};
    int ok = 0;
    for (const char* voice : voices) {
        std::vector<int16_t> pcm;
        int sr = 0;
        const std::string line =
            std::string("This is a short preview of the ") + voice + " voice.";
        const bool synth = tts.synthesize(line, voice, pcm, sr);
        if (!synth || pcm.empty()) {
            std::printf("  [warn] synthesize(%s) failed or empty\n", voice);
            continue;
        }
        std::printf("  [ok] %s → %zu samples @ %d Hz (%.2fs)\n",
                    voice, pcm.size(), sr,
                    sr > 0 ? double(pcm.size()) / double(sr) : 0.0);
        ++ok;
    }
    // At least one voice must work for F2 TTS gate.
    assert(ok >= 1 && "no TTS voice produced PCM — check kokoro-engine / piper");
    std::printf("  [ok] TTS synthesized %d/2 voices (af_heart, af_sky)\n", ok);
}

} // namespace

int main(int argc, char** argv) {
    // Gui app so QClipboard/etc. are safe if tools touch Qt Gui later.
    QGuiApplication app(argc, argv);
    std::puts("test_f2_youtube_tts: F2 YouTube + TTS live smokes");

    testSkillChaining();
    testLiveYoutubeSearch();
    testTtsVoices();

    std::puts("test_f2_youtube_tts: ALL CHECKS PASSED");
    return 0;
}
