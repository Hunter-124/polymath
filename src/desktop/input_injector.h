#pragma once
//
// InputInjector — synthesizes OS-level mouse + keyboard input via Win32 SendInput.
// Mouse coordinates are NORMALIZED 0..1 over the virtual desktop, which maps
// directly onto SendInput's 0..65535 MOUSEEVENTF_VIRTUALDESK|ABSOLUTE space, so it
// is resolution- and multi-monitor-independent (the "% that scales" the UI wants).
//
#include <string>

namespace polymath {

class InputInjector {
public:
    enum class Button { Left, Right, Middle };

    static void moveNorm(double nx, double ny);                 // 0..1 across the virtual desktop
    static void click(Button b = Button::Left);                 // at the current cursor position
    static void doubleClick(Button b = Button::Left);
    static void clickAtNorm(double nx, double ny, Button b = Button::Left);
    static void dragNorm(double x0, double y0, double x1, double y1, Button b = Button::Left);
    static void scroll(int notches);                            // + up / - down (one notch == one wheel click)

    static void typeText(const std::string& utf8);             // literal text (Unicode)
    // A key chord like "ctrl+c", "alt+tab", "win+d", "enter", "ctrl+shift+esc".
    // Unknown tokens are ignored; returns false if no real key was pressed.
    static bool keyChord(const std::string& chord);

    static Button parseButton(const std::string& s);           // "left"|"right"|"middle"
};

} // namespace polymath
