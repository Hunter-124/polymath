#include "tts_piper.h"
#include "logging.h"

#include <QProcess>
#include <QStringList>
#include <QByteArray>

#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <thread>

// miniaudio implementation lives in capture.cpp; here we only use the API.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

// TTS via the prebuilt Piper engine (piper.exe), driven as a subprocess:
//   text -> stdin ; raw s16 mono PCM -> stdout.
// This avoids building libpiper/piper-phonemize/espeak-ng from source (a painful
// chain on Windows). The engine (piper.exe + dlls + espeak-ng-data) is deployed
// to <data>/models/piper-engine/; voices live in <data>/models/piper/<voice>/.

namespace polymath::audio {

struct TtsPiper::Impl {
    std::filesystem::path voices_dir;
    std::string           default_voice;
    std::filesystem::path piper_exe;     // models/piper-engine/piper.exe

    // Read a voice's sample rate from its .onnx.json (Piper config), default 22050.
    int sampleRate(const std::filesystem::path& cfg) const {
        try {
            std::ifstream in(cfg);
            auto j = nlohmann::json::parse(in);
            if (j.contains("audio") && j["audio"].contains("sample_rate"))
                return j["audio"]["sample_rate"].get<int>();
        } catch (...) {}
        return 22050;
    }
};

TtsPiper::TtsPiper() : d_(std::make_unique<Impl>()) {}
TtsPiper::~TtsPiper() = default;

bool TtsPiper::init(const std::filesystem::path& voices_dir, const std::string& default_voice) {
    d_->voices_dir    = voices_dir;
    d_->default_voice = default_voice;
    // Engine sits next to the voices: <models>/piper-engine/piper.exe.
    d_->piper_exe = voices_dir.parent_path() / "piper-engine" / "piper.exe";
    ready_ = std::filesystem::exists(d_->piper_exe);
    if (ready_)
        PM_INFO("audio.tts: Piper engine at {} (default voice '{}')",
                d_->piper_exe.string(), default_voice);
    else
        PM_WARN("audio.tts: piper.exe not found at {} — TTS disabled "
                "(run scripts/fetch-models.ps1)", d_->piper_exe.string());
    return ready_;
}

bool TtsPiper::synthesize(const std::string& text, const std::string& voice,
                          std::vector<int16_t>& out_pcm, int& out_sample_rate) {
    out_pcm.clear();
    out_sample_rate = 22050;
    if (!ready_ || text.empty()) return false;

    // Resolve the voice files, falling back to the default voice.
    auto resolve = [&](const std::string& name) -> std::filesystem::path {
        auto model = d_->voices_dir / name / (name + ".onnx");
        return std::filesystem::exists(model) ? model : std::filesystem::path{};
    };
    std::string name = voice.empty() ? d_->default_voice : voice;
    std::filesystem::path model = resolve(name);
    if (model.empty() && name != d_->default_voice) { name = d_->default_voice; model = resolve(name); }
    if (model.empty()) {
        PM_WARN("audio.tts: voice '{}' not found under {}", name, d_->voices_dir.string());
        return false;
    }
    const auto cfg = d_->voices_dir / name / (name + ".onnx.json");
    out_sample_rate = d_->sampleRate(cfg);

    // Run piper.exe: text on stdin, raw s16 mono PCM on stdout. The working dir
    // is the engine folder so it locates espeak-ng-data/ + its DLLs.
    QProcess proc;
    proc.setWorkingDirectory(QString::fromStdString(d_->piper_exe.parent_path().string()));
    proc.setProgram(QString::fromStdString(d_->piper_exe.string()));
    proc.setArguments(QStringList{}
        << "--model"  << QString::fromStdString(model.string())
        << "--config" << QString::fromStdString(cfg.string())
        << "--output_raw");
    proc.start();
    if (!proc.waitForStarted(5000)) {
        PM_ERROR("audio.tts: failed to start piper.exe");
        return false;
    }
    proc.write(text.data(), static_cast<qint64>(text.size()));
    proc.closeWriteChannel();
    if (!proc.waitForFinished(30000)) {
        PM_ERROR("audio.tts: piper.exe timed out");
        proc.kill();
        return false;
    }
    const QByteArray raw = proc.readAllStandardOutput();
    if (raw.isEmpty()) {
        PM_ERROR("audio.tts: piper produced no audio ({})",
                 QString::fromUtf8(proc.readAllStandardError().left(200)).toStdString());
        return false;
    }
    out_pcm.resize(static_cast<size_t>(raw.size()) / sizeof(int16_t));
    std::memcpy(out_pcm.data(), raw.constData(), out_pcm.size() * sizeof(int16_t));
    return !out_pcm.empty();
}

bool TtsPiper::speak(const std::string& text, const std::string& voice) {
    std::vector<int16_t> pcm;
    int sr = 22050;
    if (!synthesize(text, voice, pcm, sr) || pcm.empty()) return false;

    // Blocking playback via a temporary miniaudio device + a tiny cursor.
    struct PlaybackState {
        const int16_t* data = nullptr;
        size_t         total = 0;
        size_t         pos = 0;
        bool           done = false;
    } state;
    state.data  = pcm.data();
    state.total = pcm.size();

    auto cb = [](ma_device* dev, void* out, const void* /*in*/, ma_uint32 frames) {
        auto* st = static_cast<PlaybackState*>(dev->pUserData);
        auto* dst = static_cast<int16_t*>(out);
        ma_uint32 i = 0;
        for (; i < frames && st->pos < st->total; ++i, ++st->pos)
            dst[i] = st->data[st->pos];
        for (; i < frames; ++i) dst[i] = 0;   // pad tail with silence
        if (st->pos >= st->total) st->done = true;
    };

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate        = static_cast<ma_uint32>(sr);
    cfg.dataCallback      = cb;
    cfg.pUserData         = &state;

    ma_device device;
    if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
        PM_ERROR("audio.tts: playback device init failed");
        return false;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        PM_ERROR("audio.tts: playback start failed");
        return false;
    }

    while (!state.done) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // flush last period

    ma_device_uninit(&device);
    PM_DEBUG("audio.tts: spoke {} samples @ {} Hz", pcm.size(), sr);
    return true;
}

} // namespace polymath::audio
