#include "ui_automation.h"
#include "logging.h"

#ifdef _WIN32
// NB: do NOT define WIN32_LEAN_AND_MEAN here — UI Automation's headers need the OLE
// bits (objbase.h defines the `interface` keyword); stripping them makes
// <uiautomation.h>'s forward-decls and definitions disagree ("different basic types").
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <objbase.h>
#  include <uiautomation.h>
#  include <string>
#endif

namespace polymath {

#ifdef _WIN32
namespace {

std::wstring toW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 1 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string fromW(BSTR b) {
    if (!b) return {};
    const int len = static_cast<int>(SysStringLen(b));
    if (len <= 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, b, len, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, b, len, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace
#endif // _WIN32

UiTarget UiAutomation::find(const std::string& target) {
    UiTarget out;
#ifdef _WIN32
    if (target.empty()) return out;

    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsCom = (hrInit == S_OK);   // S_FALSE/CHANGED_MODE => don't uninit

    IUIAutomation* automation = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IUIAutomation), reinterpret_cast<void**>(&automation)))
        || !automation) {
        if (ownsCom) CoUninitialize();
        return out;
    }

    IUIAutomationElement*   root = nullptr;
    IUIAutomationElement*   el   = nullptr;
    IUIAutomationCondition* cond = nullptr;
    VARIANT v; VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(toW(target).c_str());

    if (SUCCEEDED(automation->GetRootElement(&root)) && root &&
        SUCCEEDED(automation->CreatePropertyConditionEx(
            UIA_NamePropertyId, v, PropertyConditionFlags_IgnoreCase, &cond)) && cond) {
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, cond, &el)) && el) {
            RECT r{};
            if (SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) &&
                (r.right > r.left) && (r.bottom > r.top)) {
                const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
                const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
                const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                if (vw > 0 && vh > 0) {
                    const double cx = (r.left + r.right) / 2.0;
                    const double cy = (r.top + r.bottom) / 2.0;
                    out.nx = (cx - vx) / vw;
                    out.ny = (cy - vy) / vh;
                    BSTR name = nullptr;
                    if (SUCCEEDED(el->get_CurrentName(&name))) {
                        out.name = fromW(name);
                        SysFreeString(name);
                    }
                    out.found = (out.nx >= 0.0 && out.nx <= 1.0 &&
                                 out.ny >= 0.0 && out.ny <= 1.0);
                }
            }
        }
    }

    if (el)   el->Release();
    if (cond) cond->Release();
    if (root) root->Release();
    VariantClear(&v);
    automation->Release();
    if (ownsCom) CoUninitialize();
#else
    (void)target;
#endif
    return out;
}

} // namespace polymath
