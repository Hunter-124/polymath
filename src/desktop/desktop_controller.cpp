#include "desktop_controller.h"
#include "screen_capture.h"
#include "input_injector.h"
#include "ui_automation.h"

#include "inference_manager.h"
#include "event_bus.h"
#include "logging.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <QString>

namespace polymath {

namespace {

std::atomic<bool> g_abort{false};

void announce(const std::string& action) {
    EventBus::instance().publishDesktopControl(true, QString::fromStdString(action));
}
void logInfo(const std::string& msg) {
    EventBus::instance().publishNotice({"info", "computer", QString::fromStdString(msg)});
    PM_INFO("computer: {}", msg);
}
void logWarn(const std::string& msg) {
    EventBus::instance().publishNotice({"warn", "computer", QString::fromStdString(msg)});
    PM_WARN("computer: {}", msg);
}

} // namespace

// Pull the first {...} object out of the model's reply and parse it. The VLM is
// asked for pure JSON but small models sometimes wrap it in prose. Public static
// (declared in the header) so it can be unit-tested without the screen/input.
bool DesktopController::parseCoordJson(const std::string& reply, bool& found,
                                       double& nx, double& ny) {
    const size_t a = reply.find('{');
    const size_t b = reply.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b <= a) return false;
    try {
        auto j = nlohmann::json::parse(reply.substr(a, b - a + 1));
        found = j.value("found", true);
        double x = j.value("x", -1.0);
        double y = j.value("y", -1.0);
        if (x < 0 || y < 0) return false;
        // Accept either 0..1 fractions or 0..100 percentages.
        if (x > 1.5 || y > 1.5) { x /= 100.0; y /= 100.0; }
        nx = x; ny = y;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

DesktopController::Located DesktopController::locate(const std::string& target,
                                                    InferenceManager& inf) {
    Located out;

    // 1) Precise: Windows UI Automation (named control -> exact bounding box).
    UiTarget ui = UiAutomation::find(target);
    if (ui.found) {
        out.found = true; out.nx = ui.nx; out.ny = ui.ny;
        out.via = "ui-automation"; out.note = ui.name;
        return out;
    }

    // 2) Fallback: the local Vision model over a screenshot returns the target as a
    //    percentage of the screen (resolution-independent), which we normalize.
    ScreenShot shot = ScreenCapture::grab();
    if (!shot.ok) { out.note = "screen capture failed"; return out; }

    const std::string prompt =
        "You are a precise screen-pointing assistant. The image is the current computer "
        "screen. Locate this on-screen target: \"" + target + "\". Respond with ONLY a "
        "compact JSON object and nothing else: {\"found\": true or false, \"x\": <0-100>, "
        "\"y\": <0-100>}. x is the horizontal position as a percentage from the LEFT edge "
        "(0=left, 100=right); y is the vertical position as a percentage from the TOP edge "
        "(0=top, 100=bottom). Give the CENTER of the target. If it is not visible, set "
        "found to false.";

    std::string reply;
    try {
        reply = inf.describeImage(shot.frame, prompt);
    } catch (const std::exception& e) {
        out.note = std::string("vision model error: ") + e.what();
        return out;
    }

    bool found = false; double nx = 0, ny = 0;
    if (parseCoordJson(reply, found, nx, ny) && found) {
        out.found = true; out.nx = nx; out.ny = ny;
        out.via = "vision-model"; out.note = "located via VLM";
    } else {
        out.note = "target not found on screen";
    }
    return out;
}

bool DesktopController::click(const std::string& target, InferenceManager& inf,
                             const std::string& button, bool doubleClick) {
    if (g_abort.load()) { logWarn("aborted; ignoring click"); return false; }
    announce((doubleClick ? "Double-click: " : "Click: ") + target);

    Located loc = locate(target, inf);
    if (!loc.found) {
        logWarn("could not locate \"" + target + "\" (" + loc.note + ")");
        return false;
    }
    if (g_abort.load()) return false;
    const auto b = InputInjector::parseButton(button);
    if (doubleClick) { InputInjector::moveNorm(loc.nx, loc.ny); InputInjector::doubleClick(b); }
    else             { InputInjector::clickAtNorm(loc.nx, loc.ny, b); }
    logInfo("clicked \"" + (loc.note.empty() ? target : loc.note) + "\" via " + loc.via);
    return true;
}

bool DesktopController::clickAt(double nx, double ny, const std::string& button, bool doubleClick) {
    if (g_abort.load()) return false;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Click at %.0f%%, %.0f%%", nx * 100, ny * 100);
    announce(buf);
    const auto b = InputInjector::parseButton(button);
    if (doubleClick) { InputInjector::moveNorm(nx, ny); InputInjector::doubleClick(b); }
    else             { InputInjector::clickAtNorm(nx, ny, b); }
    logInfo(std::string(buf));
    return true;
}

void DesktopController::type(const std::string& text) {
    if (g_abort.load()) return;
    announce("Type text");
    InputInjector::typeText(text);
    logInfo("typed " + std::to_string(text.size()) + " character(s)");
}

bool DesktopController::key(const std::string& chord) {
    if (g_abort.load()) return false;
    announce("Press: " + chord);
    const bool ok = InputInjector::keyChord(chord);
    if (ok) logInfo("pressed " + chord); else logWarn("unrecognized key chord: " + chord);
    return ok;
}

void DesktopController::scroll(int notches) {
    if (g_abort.load()) return;
    announce(std::string("Scroll ") + (notches >= 0 ? "up" : "down"));
    InputInjector::scroll(notches);
    logInfo("scrolled " + std::to_string(notches) + " notch(es)");
}

std::string DesktopController::describe(InferenceManager& inf, const std::string& question) {
    ScreenShot shot = ScreenCapture::grab();
    if (!shot.ok) return "(could not capture the screen)";
    const std::string prompt = question.empty()
        ? "Describe what is currently on this computer screen. List the main interactive "
          "elements (buttons, text fields, menus, links) and any visible text that would "
          "help decide where to click."
        : question;
    try {
        return inf.describeImage(shot.frame, prompt);
    } catch (const std::exception& e) {
        return std::string("(vision model error: ") + e.what() + ")";
    }
}

void DesktopController::abort()      { g_abort.store(true); }
void DesktopController::clearAbort() { g_abort.store(false); }
bool DesktopController::aborted()    { return g_abort.load(); }

} // namespace polymath
