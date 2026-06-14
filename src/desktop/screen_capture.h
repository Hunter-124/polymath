#pragma once
//
// ScreenCapture — grabs the whole virtual desktop (all monitors) to a JPEG-backed
// Frame for the local Vision model, plus the full-resolution pixel geometry needed
// to map normalized 0..1 coordinates back onto the screen for input injection.
//
// Uses Win32 BitBlt, which (unlike Qt's QScreen::grabWindow) is safe to call from
// any thread — the agent tools run on the agent worker thread.
//
#include "types.h"

namespace polymath {

struct ScreenShot {
    Frame frame;          // downscaled JPEG + its width/height (what the VLM sees)
    int   screenW = 0;    // full virtual-desktop size in pixels (for coord mapping)
    int   screenH = 0;
    int   originX = 0;    // virtual-desktop origin (multi-monitor; may be negative)
    int   originY = 0;
    bool  ok = false;
};

class ScreenCapture {
public:
    // Grab the entire virtual desktop. `maxLongEdge` downscales the JPEG handed to
    // the VLM (it doesn't need 4K; smaller is much faster) while screenW/H stay at
    // full resolution so a normalized hit maps back to the right physical pixel.
    static ScreenShot grab(int maxLongEdge = 1366, int jpegQuality = 80);
};

} // namespace polymath
