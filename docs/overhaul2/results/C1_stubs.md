# C1 — stub notes for EV (`src/ui/tools/capture_views.cpp`)

C1 adds a confirm dialog hosted from `Main.qml`, a Safety section on
`SettingsView`, and new `app` / `notifications` surfaces. EV must mirror
these on the capture stubs or headless captures crash / look empty.

## 1. StubApp — confirm relay (mirror navigate/window)

```cpp
// Signal (flattened ConfirmRequest fields, same order as AppController):
signals:
    void confirmRequested(QString id, QString tool, QString summary,
                          QString argsPreview, QString reason);

// Invokable used by ConfirmDialog buttons:
Q_INVOKABLE void respondConfirm(const QString& id, bool approved,
                                bool alwaysAllow = false) {
    Q_UNUSED(id); Q_UNUSED(approved); Q_UNUSED(alwaysAllow);
    // no-op in capture
}

// Emitted when any path answers (dialog / notification / voice) so the dialog
// can dismiss if it was showing this id:
signals:
    void confirmSettled(QString id);
```

Optional capture demo (dev action / timed): emit once so ConfirmDialog
renders in a PNG:

```cpp
// After engine load, or behind a "--confirm-demo" flag:
emit confirmRequested(
    QStringLiteral("cap-confirm-1"),
    QStringLiteral("fs_write"),
    QStringLiteral("fs_write: C:/Users/…/Documents/notes.txt"),
    QStringLiteral("{\"path\":\"…/notes.txt\",\"content\":\"hello\"}"),
    QStringLiteral("write_local action needs your approval (standard mode)"));
```

`Main.qml` Connections call `confirmDialog.openConfirm(...)` on this signal.

## 2. StubNotifications — approve/deny + roles

`NotificationsModel` gained roles and invokables. If the capture stub is a
generic `StubListModel`, extend role names:

```
"id","severity","source","title","body","timestamp","timeLabel",
"read","category","pendingAction","confirmId"
```

Seed one pending confirm row for a populated NotificationCenter capture:

```cpp
QVariantMap{
    {"id", "cap-confirm-1"},
    {"severity", "warn"},
    {"source", "safety"},
    {"title", "Needs approval: fs_write"},
    {"body", "fs_write: C:/Users/…/Documents/notes.txt"},
    {"timestamp", qint64(0)},
    {"timeLabel", "12:00"},
    {"read", false},
    {"category", "confirm"},
    {"pendingAction", true},
    {"confirmId", "cap-confirm-1"},
}
```

Invokables (no-op is fine):

```cpp
Q_INVOKABLE void approveConfirm(const QString& confirmId) { Q_UNUSED(confirmId); }
Q_INVOKABLE void denyConfirm(const QString& confirmId) { Q_UNUSED(confirmId); }
```

## 3. SettingsView Safety section

Uses **free-form** `settings.getString` / `setString` / `getBool` / `setBool`
only — no new typed `Q_PROPERTY` on SettingsController. StubSettings already
implements those generics; ensure these keys return sensible defaults:

| key | suggested stub default |
|-----|------------------------|
| `safety.mode` | `"standard"` |
| `safety.fs_allowed_roots` | `"Documents;Desktop;Downloads;@data"` |
| `safety.audit` | `"1"` / true |
| `safety.cmd_denylist` | any non-empty string (shows in read-only viewer) |
| `safety.tool_overrides` | `""` or `"run_command"` to exercise the list UI |

No new `views[]` entry — Settings is already captured; Safety is an extra
section (scroll down / `focusSection: "safety"`).

## 4. ConfirmDialog.qml registration

`src/ui/CMakeLists.txt` lists `qml/ConfirmDialog.qml` in the Polymath QML
module. Capture loads Main via the same module resources — no extra path
work if capture links `pm_ui`.

## 5. NotificationCenter inline buttons (optional follow-up)

`NotificationCenter.qml` is **not** owned by C1 and still has no Approve/Deny
row chrome. Model roles `pendingAction` + `confirmId` and invokables
`notifications.approveConfirm(id)` / `denyConfirm(id)` are ready; a later
UI pass can render buttons when `category === "confirm" && pendingAction`.
Until then, the ConfirmDialog is the primary approval surface; the center
still shows the pending item.
