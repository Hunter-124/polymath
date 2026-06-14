#pragma once
//
// UiAutomation — precise UI-element targeting via the Windows UI Automation tree.
// Given a target name ("Submit", "Address and search bar", ...), it finds the
// matching control's bounding rectangle and returns its CENTER as a normalized
// 0..1 point over the virtual desktop — no pixel guessing. Standard Windows
// controls expose accessible names; for non-accessible surfaces (canvas, games,
// custom-drawn UIs) this returns found=false and the caller falls back to the VLM.
//
#include <string>

namespace polymath {

struct UiTarget {
    bool        found = false;
    double      nx = 0.0;   // normalized 0..1 over the virtual desktop
    double      ny = 0.0;
    std::string name;       // the matched element's accessible name
};

class UiAutomation {
public:
    // Exact, case-insensitive match on the element's Name property. COM is set up
    // and torn down per call on the calling (agent worker) thread.
    static UiTarget find(const std::string& target);
};

} // namespace polymath
