# C1 — Confirmation UX + Safety settings: contract summary

## Surfaces

| Piece | Role |
|-------|------|
| `ConfirmDialog.qml` | Glass modal: tool, summary, reason, args preview; Approve / Deny / Always allow |
| `Main.qml` | Hosts dialog; `Connections` on `app.confirmRequested` (+ toast) |
| `AppController` | Relays `EventBus::confirmRequested` → QML; `respondConfirm` → `publishConfirmResponse` |
| `NotificationsModel` | Pending confirm rows (`category=confirm`); `approveConfirm` / `denyConfirm` |
| `SettingsView` ▸ Safety | mode, audit, allowed roots editor, overrides list, denylist viewer |
| `SafetyPolicy` | Honors `safety.tool_overrides` after hard Deny checks |
| Config | `keys::SafetyToolOverrides` (`safety.tool_overrides`, default `""`) |

## EventBus (unchanged from A4)

```cpp
struct ConfirmRequest  { QString id, tool, summary, args_preview, reason; };
struct ConfirmResponse { QString id; bool approved = false; bool always_allow = false; };
// publishConfirmRequest / publishConfirmResponse
// signals: confirmRequested, confirmResponse
```

## AppController API (QML)

```cpp
// bus → QML (queued onto UI thread via AppController affinity)
signal confirmRequested(QString id, QString tool, QString summary,
                        QString argsPreview, QString reason);

// QML → bus (AgentLoop already subscribed to confirmResponse)
Q_INVOKABLE void respondConfirm(const QString& id, bool approved,
                                bool alwaysAllow = false);
```

### `respondConfirm` behavior

1. If `approved && alwaysAllow`: look up tool name from the pending map
   (filled when `confirmRequested` arrived) and append it to
   `safety.tool_overrides` (semicolon list, de-duped) via Config.
2. Publish `ConfirmResponse{ id, approved, always_allow: approved&&alwaysAllow }`.
3. Any `confirmResponse` (dialog / notification / voice) clears the pending map entry.

## `safety.tool_overrides`

- Format: `;`-separated exact tool names (e.g. `fs_write;run_command`).
- Parsed in `SafetyPolicy::refresh` into `Compiled::tool_overrides`.
- In `SafetyPolicy::check`, **after** path Deny, command Deny, and write-size
  Deny: if `tool` is in the set → `{Allow, "tool is always allowed (user override)"}`.
- **Deny still wins** for outside-roots / denied globs / cmd denylist / write cap.
- Overrides skip the risk/mode Confirm gate and the `schedule_task` recurring Confirm.
- Written by ConfirmDialog “Always allow this tool” via `respondConfirm(..., true)`.
- Editable (remove) in Settings ▸ Safety.

## Notifications

- On `confirmRequested`: prepend a warn row (`source=safety`, `category=confirm`,
  `pendingAction=true`, `id`/`confirmId` = request id).
- `approveConfirm(id)` / `denyConfirm(id)` publish `ConfirmResponse` (no always_allow).
- On any `confirmResponse`: remove the matching row.
- NotificationCenter chrome for inline buttons is a follow-up (see `C1_stubs.md`).

## Settings ▸ Safety keys

| Key | UI |
|-----|----|
| `safety.mode` | Combo Strict / Standard / Trusted + plain-English blurb |
| `safety.fs_allowed_roots` | List + add/remove (semicolon-backed) |
| `safety.audit` | Switch |
| `safety.cmd_denylist` | Read-only viewer |
| `safety.tool_overrides` | List + remove (when non-empty) |

Deep-link: `openSettings("safety")` / command palette “Settings: Safety”.

## Out of scope (this node)

- Editing `NotificationCenter.qml` for inline Approve/Deny buttons.
- AgentLoop changes (`always_allow` is consumed only for Config write on the UI side;
  resume still uses `approved` only — next call auto-Allows via policy overrides).
- ActivityLog “recent gated actions” browser (skipped as optional/hard).
- capture_views / EV (documented in `C1_stubs.md`).
