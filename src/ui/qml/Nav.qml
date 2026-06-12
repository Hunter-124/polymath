pragma Singleton
import QtQuick

// Nav — a tiny app-wide message bus for the QML layer.  Views are loaded in
// isolation (Loaders, and standalone in the capture harness), so they cannot
// reach ids inside Main.qml; instead they raise these signals and Main.qml —
// when present — reacts.  Standalone, the signals are simply unconnected.
QtObject {
    // Ask the shell to switch to a page by name ("Dashboard", "Chat", ...).
    signal navigate(string page)

    // Which Settings sub-section is open; SettingsView binds to this so the
    // command palette can deep-link straight to e.g. Models.
    property string settingsSection: "Personalities"

    // Convenience: jump to a Settings sub-section in one call.
    function goSettings(section) {
        settingsSection = section
        navigate("Settings")
    }

    // Surface a toast through the shell ("info" | "warn" | "error" | "good").
    signal notify(string level, string source, string message)

    // Ask the shell to focus the chat composer (Dashboard quick-ask hand-off).
    signal focusChat()
}
