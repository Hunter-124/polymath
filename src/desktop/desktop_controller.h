#pragma once
//
// DesktopController — the "computer use" brain. Turns high-level intents ("click
// the Submit button", "type hello", "press ctrl+s") into real OS input by:
//   1. LOCATING a target: Windows UI Automation first (precise, no guessing), then
//      the local Vision model over a screenshot (approximate, works on anything).
//   2. ACTING via SendInput (move/click/drag/scroll/type/key).
// Every action publishes the EventBus "desktop control" state (so the UI shows a
// glowing border) + an activity-log line, and is a no-op while aborted() is set
// (the UI's panic-stop). All static + stateless except the abort flag; safe to
// call from the agent worker thread.
//
#include <string>

namespace polymath {

class InferenceManager;

class DesktopController {
public:
    struct Located {
        bool        found = false;
        double      nx = 0.0, ny = 0.0;   // normalized 0..1 over the virtual desktop
        std::string via;                  // "ui-automation" | "vision-model"
        std::string note;                 // matched element name / VLM note
    };
    static Located locate(const std::string& target, InferenceManager& inf);

    static bool click(const std::string& target, InferenceManager& inf,
                      const std::string& button = "left", bool doubleClick = false);
    static bool clickAt(double nx, double ny, const std::string& button = "left",
                        bool doubleClick = false);
    static void type(const std::string& text);
    static bool key(const std::string& chord);
    static void scroll(int notches);
    // VLM description of the current screen (so the agent can "see" before acting).
    static std::string describe(InferenceManager& inf, const std::string& question = "");

    static void abort();        // panic-stop (set from the UI / overlay)
    static void clearAbort();
    static bool aborted();

    // Parse the Vision model's coordinate reply: pulls the first {...} object out
    // of `reply` (small models often wrap the JSON in prose), reads {found,x,y},
    // and normalizes x/y to 0..1 (accepts either 0..1 fractions or 0..100 %).
    // Returns false on no/!found/malformed/negative coordinates. Pure logic —
    // exposed for tests (no screen capture, no input). See locate().
    static bool parseCoordJson(const std::string& reply, bool& found,
                               double& nx, double& ny);
};

} // namespace polymath
