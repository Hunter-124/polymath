#include "global_hotkey.h"

#include <QCoreApplication>

#ifdef _WIN32
// WIN32_LEAN_AND_MEAN is already defined project-wide; only RegisterHotKey /
// UnregisterHotKey / MSG / WM_HOTKEY are needed here (all in windows.h).
#include <windows.h>
#endif

namespace polymath {

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {
    if (auto* appInst = QCoreApplication::instance())
        appInst->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey() {
    unregister();
    if (auto* appInst = QCoreApplication::instance())
        appInst->removeNativeEventFilter(this);
}

bool GlobalHotkey::registerHotkey(unsigned int mods, unsigned int vk) {
#ifdef _WIN32
    unregister();
    // MOD_NOREPEAT (0x4000) so holding the chord fires exactly once. hwnd=NULL
    // registers against the calling thread; WM_HOTKEY then arrives in this
    // thread's message queue, which Qt's dispatcher feeds to native filters.
    // Returns false silently if the chord is taken — the caller tries fallbacks
    // and logs the final outcome, so per-attempt warnings would be misleading.
    if (::RegisterHotKey(nullptr, id_, mods | 0x4000, vk)) {
        registered_ = true;
        return true;
    }
    return false;
#else
    (void)mods; (void)vk;
    return false;
#endif
}

void GlobalHotkey::unregister() {
#ifdef _WIN32
    if (registered_) {
        ::UnregisterHotKey(nullptr, id_);
        registered_ = false;
    }
#endif
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message,
                                     qintptr* /*result*/) {
#ifdef _WIN32
    if (message && eventType == QByteArrayLiteral("windows_generic_MSG")) {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == id_) {
            emit triggered();
            // Don't consume it — nothing else listens for this id, and swallowing
            // foreign messages would be rude.
        }
    }
#else
    (void)eventType; (void)message;
#endif
    return false;
}

} // namespace polymath
