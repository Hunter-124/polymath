#include "input_injector.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace polymath {

#ifdef _WIN32
namespace {

// Map a normalized 0..1 desktop coordinate to SendInput's 0..65535 absolute space.
LONG toAbs(double n) {
    n = std::clamp(n, 0.0, 1.0);
    return static_cast<LONG>(n * 65535.0 + 0.5);
}

void sendMouse(DWORD flags, LONG ax = 0, LONG ay = 0, DWORD data = 0) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = ax; in.mi.dy = ay;
    in.mi.mouseData = data;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(INPUT));
}

void buttonFlags(InputInjector::Button b, DWORD& down, DWORD& up) {
    switch (b) {
        case InputInjector::Button::Right:  down = MOUSEEVENTF_RIGHTDOWN;  up = MOUSEEVENTF_RIGHTUP;  break;
        case InputInjector::Button::Middle: down = MOUSEEVENTF_MIDDLEDOWN; up = MOUSEEVENTF_MIDDLEUP; break;
        default:                            down = MOUSEEVENTF_LEFTDOWN;   up = MOUSEEVENTF_LEFTUP;   break;
    }
}

void keyVk(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

// Map a chord token to a virtual-key code (0 == unknown).
WORD vkForToken(const std::string& t) {
    if (t.size() == 1) {
        char c = t[0];
        if (c >= 'a' && c <= 'z') return static_cast<WORD>('A' + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return static_cast<WORD>(c);
        if (c >= '0' && c <= '9') return static_cast<WORD>(c);
    }
    if (t == "ctrl" || t == "control") return VK_CONTROL;
    if (t == "alt")                    return VK_MENU;
    if (t == "shift")                  return VK_SHIFT;
    if (t == "win" || t == "super" || t == "meta") return VK_LWIN;
    if (t == "enter" || t == "return") return VK_RETURN;
    if (t == "tab")    return VK_TAB;
    if (t == "esc" || t == "escape") return VK_ESCAPE;
    if (t == "space" || t == "spacebar") return VK_SPACE;
    if (t == "backspace" || t == "back") return VK_BACK;
    if (t == "delete" || t == "del") return VK_DELETE;
    if (t == "home")  return VK_HOME;
    if (t == "end")   return VK_END;
    if (t == "pageup" || t == "pgup")     return VK_PRIOR;
    if (t == "pagedown" || t == "pgdn")   return VK_NEXT;
    if (t == "up")    return VK_UP;
    if (t == "down")  return VK_DOWN;
    if (t == "left")  return VK_LEFT;
    if (t == "right") return VK_RIGHT;
    if (t == "insert" || t == "ins") return VK_INSERT;
    if (t == "printscreen" || t == "prtsc") return VK_SNAPSHOT;
    if (t.size() >= 2 && (t[0] == 'f' || t[0] == 'F') && std::isdigit((unsigned char)t[1])) {
        int n = std::atoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) return static_cast<WORD>(VK_F1 + (n - 1));
    }
    return 0;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace
#endif // _WIN32

InputInjector::Button InputInjector::parseButton(const std::string& s) {
    if (s == "right")  return Button::Right;
    if (s == "middle") return Button::Middle;
    return Button::Left;
}

void InputInjector::moveNorm(double nx, double ny) {
#ifdef _WIN32
    sendMouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK,
              toAbs(nx), toAbs(ny));
#else
    (void)nx; (void)ny;
#endif
}

void InputInjector::click(Button b) {
#ifdef _WIN32
    DWORD down, up; buttonFlags(b, down, up);
    sendMouse(down); sendMouse(up);
#else
    (void)b;
#endif
}

void InputInjector::doubleClick(Button b) {
    click(b);
    click(b);
}

void InputInjector::clickAtNorm(double nx, double ny, Button b) {
    moveNorm(nx, ny);
    click(b);
}

void InputInjector::dragNorm(double x0, double y0, double x1, double y1, Button b) {
#ifdef _WIN32
    DWORD down, up; buttonFlags(b, down, up);
    moveNorm(x0, y0);
    sendMouse(down);
    moveNorm(x1, y1);
    sendMouse(up);
#else
    (void)x0; (void)y0; (void)x1; (void)y1; (void)b;
#endif
}

void InputInjector::scroll(int notches) {
#ifdef _WIN32
    sendMouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(notches * WHEEL_DELTA));
#else
    (void)notches;
#endif
}

void InputInjector::typeText(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return;
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return;
    std::vector<wchar_t> w(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), wlen);
    for (int i = 0; i < wlen - 1; ++i) {   // wlen-1 drops the trailing NUL
        for (bool up : {false, true}) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wScan = w[i];
            in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
            SendInput(1, &in, sizeof(INPUT));
        }
    }
#else
    (void)utf8;
#endif
}

bool InputInjector::keyChord(const std::string& chord) {
#ifdef _WIN32
    std::vector<WORD> keys;
    std::string tok;
    auto flush = [&] {
        if (tok.empty()) return;
        WORD vk = vkForToken(lower(tok));
        if (vk) keys.push_back(vk);
        tok.clear();
    };
    for (char c : chord) { if (c == '+' || c == '-' || c == ' ') flush(); else tok += c; }
    flush();
    if (keys.empty()) return false;
    for (WORD vk : keys) keyVk(vk, /*up*/ false);             // press in order
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) keyVk(*it, /*up*/ true);  // release reverse
    return true;
#else
    (void)chord;
    return false;
#endif
}

} // namespace polymath
