# E5 — Copy generalization + small polish

**Owns only:** `ShoppingView.qml` empty-state, `ChatView.qml` empty-state prompts,
`PrivacyView.qml` screen-capture toggle. No E1 chat-selection touch.

## Changes

### 1. Shopping empty-state (`ShoppingView.qml`)
- **Icon:** unchanged (`iconName: "cart"`)
- **hint:** → `Add anything you need to buy — say 'add AA batteries to my list'.`
- Title left as "Your shopping list is empty"

### 2. Chat empty-state (`ChatView.qml`)
- **hint** examples refreshed for new powers:
  - watch video → `"watch this YouTube video"`
  - schedule → `"schedule a briefing at 9am"`
  - screen glance → `"what's on my screen?"`
  - create file → `"create a file called ideas.txt"`
- E1 `TextEdit` selection / context menu / ListView flick path untouched

### 3. PrivacyView screen capture (`PrivacyView.qml`)
- New `Toggle` row after Cameras, before Encrypt:
  - `keyName: "privacy.screen_capture"` (C3 `keys::ScreenCapture`)
  - caption: "Screen capture"
  - sub: "AI can glance at your screen when asked"
- Same pattern as mic/camera: `app.privacy(keyName)` / `app.setPrivacy(keyName, checked)`
- Default ON is seeded by C3; UI only surfaces the key

## Accept
Headless captures of Shopping / Chat (empty) / Privacy should show the new copy
and the Screen capture switch.
