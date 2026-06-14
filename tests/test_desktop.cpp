// Computer-use unit tests — the pure-logic helpers that decide WHERE the
// assistant clicks. No screen capture, no SendInput: this is the safe,
// deterministic core of an otherwise live-only subsystem (the live click loop
// needs a real desktop + the Vision model + a human, so it can't run here).
//
//   * DesktopController::parseCoordJson — parse the Vision model's coordinate
//     reply: clean JSON, prose-wrapped JSON, 0..1 fractions vs 0..100 %, and the
//     not-found / no-JSON / malformed / negative rejects. A bug here is a wrong
//     click on the user's machine, so it's the highest-value thing to pin down.
//   * InputInjector::parseButton — button-name mapping + safe default.

#include "desktop_controller.h"
#include "input_injector.h"

#undef NDEBUG   // keep assert() live in Release
#include <cassert>
#include <cstdio>
#include <string>

using namespace polymath;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("== desktop (computer-use) unit ==");

    bool found = false;
    double nx = -1, ny = -1;

    // 1. Clean percentage JSON -> normalized 0..1 center.
    found = false; nx = ny = -1;
    assert(DesktopController::parseCoordJson(R"({"found": true, "x": 50, "y": 25})", found, nx, ny));
    assert(found);
    assert(nx > 0.49 && nx < 0.51 && ny > 0.24 && ny < 0.26);
    std::puts("  [ok] percentage coords -> 0..1");

    // 2. Fractions (already 0..1) pass through unscaled; `found` defaults true.
    found = false; nx = ny = -1;
    assert(DesktopController::parseCoordJson(R"({"x": 0.8, "y": 0.1})", found, nx, ny));
    assert(found);
    assert(nx > 0.79 && nx < 0.81 && ny > 0.09 && ny < 0.11);
    std::puts("  [ok] fractional coords pass through");

    // 3. JSON wrapped in prose (small models do this) is still extracted.
    found = false; nx = ny = -1;
    assert(DesktopController::parseCoordJson(
        "Sure! The button is here: {\"found\": true, \"x\": 10, \"y\": 90}. Hope that helps!",
        found, nx, ny));
    assert(nx > 0.09 && nx < 0.11 && ny > 0.89 && ny < 0.91);
    std::puts("  [ok] prose-wrapped JSON extracted");

    // 4. found:false (no coords) -> rejected (treated as not-located by locate()).
    found = true; nx = ny = -1;
    assert(!DesktopController::parseCoordJson(R"({"found": false})", found, nx, ny));

    // 5. No JSON at all -> false.
    assert(!DesktopController::parseCoordJson("I cannot see that on the screen.", found, nx, ny));

    // 6. Malformed JSON (unquoted keys) -> false (no throw).
    assert(!DesktopController::parseCoordJson("{x: 50, y: 25}", found, nx, ny));

    // 7. Negative coords -> false.
    assert(!DesktopController::parseCoordJson(R"({"x": -5, "y": 10})", found, nx, ny));
    std::puts("  [ok] not-found / no-JSON / malformed / negative rejected");

    // --- InputInjector::parseButton: name -> Button, with a safe default. ---
    assert(InputInjector::parseButton("left")    == InputInjector::Button::Left);
    assert(InputInjector::parseButton("right")   == InputInjector::Button::Right);
    assert(InputInjector::parseButton("middle")  == InputInjector::Button::Middle);
    assert(InputInjector::parseButton("nonsense") == InputInjector::Button::Left);
    std::puts("  [ok] parseButton mapping + default");

    std::puts("test_desktop: OK");
    return 0;
}
