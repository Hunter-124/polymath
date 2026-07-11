#include "screen_tools.h"
#include "config.h"
#include "database.h"
#include "inference_manager.h"
#include "logging.h"
#include "paths.h"
#include "types.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <string>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <cwctype>
#endif

// screen_capture / screen_describe — on-demand desktop grabs for the agent.
// Privacy: keys::ScreenCapture (privacy.screen_capture), master-gated like the
// other sense toggles. Capture path is always durable so the skill is useful
// even when no vision model is loaded (describe falls back to a path note).

namespace polymath {

namespace {

// Result of a single grab attempt (image still in memory + path on disk).
struct CaptureResult {
    bool        ok = false;
    QImage      image;
    std::string path;
    int         width = 0;
    int         height = 0;
    std::string error;
    std::string note;   // non-fatal extra (e.g. window_title ignored)
};

// Schema shared by screen_capture and screen_describe.
nlohmann::json captureArgsSchema() {
    return {
        {"type", "object"},
        {"properties", {
            {"monitor", {
                {"type", "integer"},
                {"description",
                 "0-based monitor index (default: primary screen). "
                 "Use when multiple displays are attached."}}},
            {"window_title", {
                {"type", "string"},
                {"description",
                 "Optional window title substring. On Windows, grabs that window "
                 "if found (case-insensitive partial match); otherwise returns an "
                 "error. Ignored on other platforms with a note."}}},
        }},
    };
}

// Privacy gate — same error string the DAG / Privacy settings expect.
ToolResult privacyDenied() {
    return {false,
            {{"error", "screen capture disabled in Privacy settings"},
             {"ok", false}},
            "screen capture disabled in Privacy settings"};
}

bool screenCaptureEnabled(ToolContext& ctx) {
    if (!ctx.db) return false;
    Config cfg(*ctx.db);
    // Default ON when the key is missing (pre-seed / older DBs) so a missing
    // row does not silently block capture until the next seedDefaults pass.
    if (ctx.db->getSetting(keys::ScreenCapture, "1") == "0")
        return false;
    if (Config::isMasterGated(keys::ScreenCapture) && !cfg.masterEnabled())
        return false;
    return true;
}

// Ready-made ui_control suggestion so the model can spawn an image surface.
nlohmann::json imageUiHint(const std::string& path) {
    return {
        {"action", "spawn_surface"},
        {"type", "image"},
        {"title", "Screen capture"},
        {"args", {{"path", path}}},
    };
}

// Best-effort prune of captures/ older than 7 days (DAG accept criteria).
void pruneOldCaptures(const std::filesystem::path& dir) {
    const QDir qdir(QString::fromStdString(dir.string()));
    if (!qdir.exists()) return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    const QFileInfoList files = qdir.entryInfoList(
        QStringList{QStringLiteral("screen_*.png")},
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : files) {
        if (fi.lastModified().isValid() && fi.lastModified() < cutoff)
            QFile::remove(fi.absoluteFilePath());
    }
}

#ifdef Q_OS_WIN
// Case-insensitive substring match over top-level visible windows.
struct FindWindowCtx {
    std::wstring needle;
    HWND         found = nullptr;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lparam) {
    auto* ctx = reinterpret_cast<FindWindowCtx*>(lparam);
    if (!IsWindowVisible(hwnd)) return TRUE;
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;
    std::wstring title(static_cast<size_t>(len) + 1, L'\0');
    const int got = GetWindowTextW(hwnd, title.data(), len + 1);
    if (got <= 0) return TRUE;
    title.resize(static_cast<size_t>(got));
    // Case-insensitive find.
    auto lower = [](std::wstring s) {
        for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
        return s;
    };
    if (lower(title).find(lower(ctx->needle)) != std::wstring::npos) {
        ctx->found = hwnd;
        return FALSE;  // stop
    }
    return TRUE;
}

HWND findWindowByTitleSubstring(const std::string& utf8Title) {
    if (utf8Title.empty()) return nullptr;
    const QString q = QString::fromStdString(utf8Title);
    FindWindowCtx ctx;
    ctx.needle = q.toStdWString();
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}
#endif

// Resolve screen + optional window, grab pixels, write PNG under data/captures/.
CaptureResult grabScreen(const nlohmann::json& args) {
    CaptureResult out;

    if (!QGuiApplication::instance()) {
        out.error = "no QGuiApplication (screen capture requires a GUI session)";
        return out;
    }

    const std::string windowTitle = args.value("window_title", "");
    QImage img;
    QString sourceLabel;

    if (!windowTitle.empty()) {
#ifdef Q_OS_WIN
        HWND hwnd = findWindowByTitleSubstring(windowTitle);
        if (!hwnd) {
            out.error = "window not found for title: " + windowTitle;
            return out;
        }
        // grabWindow accepts a platform WId (HWND on Windows).
        QScreen* scr = QGuiApplication::primaryScreen();
        if (!scr) {
            out.error = "no primary screen";
            return out;
        }
        img = scr->grabWindow(reinterpret_cast<WId>(hwnd)).toImage();
        sourceLabel = QStringLiteral("window:%1").arg(QString::fromStdString(windowTitle));
#else
        out.note = "window_title not supported on this platform; capturing primary monitor";
        QScreen* scr = QGuiApplication::primaryScreen();
        if (!scr) {
            out.error = "no primary screen";
            return out;
        }
        img = scr->grabWindow(0).toImage();
        sourceLabel = QStringLiteral("primary");
#endif
    } else {
        const QList<QScreen*> screens = QGuiApplication::screens();
        if (screens.isEmpty()) {
            out.error = "no screens available";
            return out;
        }
        QScreen* scr = nullptr;
        if (args.contains("monitor") && args["monitor"].is_number_integer()) {
            const int idx = args["monitor"].get<int>();
            if (idx < 0 || idx >= screens.size()) {
                out.error = "monitor index out of range (0.." +
                            std::to_string(screens.size() - 1) + ")";
                return out;
            }
            scr = screens.at(idx);
            sourceLabel = QStringLiteral("monitor:%1").arg(idx);
        } else {
            scr = QGuiApplication::primaryScreen();
            if (!scr) scr = screens.front();
            sourceLabel = QStringLiteral("primary");
        }
        if (!scr) {
            out.error = "no screen resolved";
            return out;
        }
        img = scr->grabWindow(0).toImage();
    }

    if (img.isNull()) {
        out.error = "grabWindow returned empty image";
        return out;
    }

    // Persist under <data root>/captures/screen_YYYYMMDD_HHMMSS_mmm.png
    namespace fs = std::filesystem;
    const fs::path capDir = Paths::instance().root() / "captures";
    std::error_code ec;
    fs::create_directories(capDir, ec);
    if (ec) {
        out.error = "cannot create captures dir: " + ec.message();
        return out;
    }
    pruneOldCaptures(capDir);

    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const fs::path filePath = capDir / ("screen_" + stamp.toStdString() + ".png");
    const QString qPath = QString::fromStdString(filePath.string());
    if (!img.save(qPath, "PNG")) {
        out.error = "failed to save PNG to " + filePath.string();
        return out;
    }

    out.ok = true;
    out.image = std::move(img);
    out.path = filePath.string();
    out.width = out.image.width();
    out.height = out.image.height();
    PM_INFO("screen_capture: saved {} ({}x{}, {})",
            out.path, out.width, out.height, sourceLabel.toStdString());
    return out;
}

// Encode the grabbed QImage as JPEG bytes for InferenceManager::describeImage.
Frame frameFromImage(const QImage& img) {
    Frame f;
    f.width = img.width();
    f.height = img.height();
    f.ts = Clock::now();
    if (img.isNull()) return f;

    QImage rgb = img;
    if (rgb.format() != QImage::Format_RGB888 && rgb.format() != QImage::Format_RGB32 &&
        rgb.format() != QImage::Format_ARGB32 && rgb.format() != QImage::Format_RGBA8888) {
        rgb = img.convertToFormat(QImage::Format_RGB888);
    }
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    if (!rgb.save(&buf, "JPG", 85)) {
        PM_WARN("screen_describe: JPEG encode failed");
        return f;
    }
    f.jpeg.assign(ba.begin(), ba.end());
    return f;
}

} // namespace

// --- screen_capture ---------------------------------------------------------

std::string ScreenCaptureTool::name() const { return "screen_capture"; }

std::string ScreenCaptureTool::description() const {
    return "Capture the desktop (primary monitor by default, or a monitor index / "
           "window title) to a PNG under the app data captures/ folder. Returns "
           "path, width, height. Privacy-gated by privacy.screen_capture. Use "
           "ui_hint / ui_control spawn_surface type=image to show the capture.";
}

nlohmann::json ScreenCaptureTool::parametersSchema() const {
    return captureArgsSchema();
}

ToolResult ScreenCaptureTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!screenCaptureEnabled(ctx))
        return privacyDenied();

    CaptureResult cap = grabScreen(args);
    if (!cap.ok) {
        return {false,
                {{"error", cap.error}, {"ok", false}},
                "screen_capture: " + cap.error};
    }

    nlohmann::json content = {
        {"ok", true},
        {"path", cap.path},
        {"width", cap.width},
        {"height", cap.height},
        {"ui_hint", imageUiHint(cap.path)},
    };
    if (!cap.note.empty())
        content["note"] = cap.note;

    return {true, std::move(content),
            "Captured screen " + std::to_string(cap.width) + "x" +
                std::to_string(cap.height) + " -> " + cap.path};
}

// --- screen_describe --------------------------------------------------------

std::string ScreenDescribeTool::name() const { return "screen_describe"; }

std::string ScreenDescribeTool::description() const {
    return "Capture the desktop (same args as screen_capture) then describe what "
           "is on screen via the vision model when loaded. Always saves the PNG; "
           "if no vision model is available returns the path with a clear note. "
           "Privacy-gated by privacy.screen_capture.";
}

nlohmann::json ScreenDescribeTool::parametersSchema() const {
    return captureArgsSchema();
}

ToolResult ScreenDescribeTool::invoke(const nlohmann::json& args, ToolContext& ctx) {
    if (!screenCaptureEnabled(ctx))
        return privacyDenied();

    CaptureResult cap = grabScreen(args);
    if (!cap.ok) {
        return {false,
                {{"error", cap.error}, {"ok", false}},
                "screen_describe: " + cap.error};
    }

    nlohmann::json content = {
        {"ok", true},
        {"path", cap.path},
        {"width", cap.width},
        {"height", cap.height},
        {"ui_hint", imageUiHint(cap.path)},
    };
    if (!cap.note.empty())
        content["note"] = cap.note;

    std::string description;
    std::string visionNote;
    bool visionOk = false;

    if (!ctx.inference) {
        visionNote = "vision model not loaded; image saved at " + cap.path;
    } else {
        Frame frame = frameFromImage(cap.image);
        if (frame.jpeg.empty()) {
            visionNote = "vision model not loaded; image saved at " + cap.path
                         + " (JPEG encode failed)";
        } else {
            const std::string prompt =
                "Describe what is visible on this computer screen. Be concise but "
                "specific: name open applications/windows if recognizable, main "
                "content, and anything the user appears to be working on. "
                "If text is readable, summarize the important bits.";
            try {
                description = ctx.inference->describeImage(frame, prompt);
            } catch (const std::exception& e) {
                PM_WARN("screen_describe: describeImage threw: {}", e.what());
                description.clear();
            }
            if (description.empty()) {
                visionNote = "vision model not loaded; image saved at " + cap.path;
            } else {
                visionOk = true;
            }
        }
    }

    if (visionOk) {
        content["description"] = description;
        content["vision_ok"] = true;
        return {true, std::move(content), description};
    }

    content["description"] = nullptr;
    content["vision_ok"] = false;
    content["vision_note"] = visionNote;
    return {true, std::move(content), visionNote};
}

} // namespace polymath
