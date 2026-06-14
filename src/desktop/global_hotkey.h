#pragma once
//
// GlobalHotkey — a system-wide hotkey that fires even when Hearth is not the
// focused application.  Backs the "quick ask" pop-over: press the chord from
// anywhere and an input box appears over whatever you're doing.
//
// Windows-only: it registers the chord with RegisterHotKey() and watches the
// thread's message queue for WM_HOTKEY via a QAbstractNativeEventFilter.  Must
// be constructed and registered on the GUI thread (where QGuiApplication's
// event dispatcher pumps thread messages).  On non-Windows it degrades to a
// no-op (registerHotkey returns false) so the rest of the app still builds.
//
#include <QObject>
#include <QAbstractNativeEventFilter>

namespace polymath {

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    // Modifier bits mirror the Win32 MOD_* values so callers don't need windows.h.
    enum Mod { Alt = 0x0001, Control = 0x0002, Shift = 0x0004, Win = 0x0008 };

    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    // Registers (replacing any previous) the chord `mods` + virtual-key `vk`.
    // Returns false if the OS refused it (e.g. another app already owns it) or
    // on non-Windows. Auto-repeat is suppressed so holding the chord fires once.
    bool registerHotkey(unsigned int mods, unsigned int vk);
    void unregister();
    bool registered() const { return registered_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

signals:
    void triggered();

private:
    int  id_ = 1;          // RegisterHotKey id, unique within this thread
    bool registered_ = false;
};

} // namespace polymath
