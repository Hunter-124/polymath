# E1 — Chat: text selection + drag-scroll coexistence

**File edited:** `src/ui/qml/ChatView.qml` only (per node ownership; nothing else
touched — empty-state string at line ~257 is left as-is, that's E5's edit after
E1 lands per the DAG file-ordering note).

## 1. Control swap: `Label` → `TextEdit`

The message body (`id: msg`, was a plain `Label` at old lines 104-112) is now:

```qml
TextEdit {
    id: msg
    Layout.fillWidth: true
    text: bubbleRow.text
    textFormat: TextEdit.MarkdownText
    readOnly: true
    selectByMouse: true
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    color: Style.text
    selectionColor: Style.sectionColor("Chat")
    selectedTextColor: Style.accentText
    font.family: Style.fontFamily
    font.pixelSize: Style.fsBody
}
```

**textFormat choice: `TextEdit.MarkdownText`, kept (not falling back to
PlainText).** Rationale:

- It's the spec's primary instruction and matches the app-wide convention the
  DAG already commits to elsewhere (E3's `NoteSurface` also renders AI-authored
  content via `Text.MarkdownText`), so assistant messages that use lists/bold/
  code-fences (increasingly likely post-A1's final-answer cleanup) render
  properly instead of showing raw `**`/`-`/backtick syntax.
- Risk is low for plain sentences: QML's Markdown parser needs *paired*
  delimiters to trigger emphasis, so stray single `_`/`*` characters inside
  ordinary words (`session_digest`, `list_schedules`) do not get misinterpreted
  — only genuine matched-pair markdown gets rendered.
- **Caveat / open item:** I could not empirically screenshot-verify this per
  the node's STRICT RULES ("Do NOT run cmake/build — orchestrator builds
  centrally"). The prebuilt `build/cpu/bin/Release/capture_views.exe` and
  `build/cuda/bin/capture_views.exe` in the tree predate this edit and would
  only re-render the old `Label`, so running them now would not exercise the
  new code path. **Ask for EV (which owns `capture_views.cpp` and runs the
  verify pass) to specifically eyeball a ChatView capture with a message
  containing literal `*`, `_`, `#`, or `` ` `` characters** (e.g. a
  troubleshooting message mentioning a snake_case identifier or a shell flag
  like `-rf`) and swap the one line to `textFormat: TextEdit.PlainText` if it
  visibly mangles a bubble. Streaming caveat: while a message is still
  streaming, `text` updates mid-token, so an unterminated `**bold` or code
  fence can render oddly for a frame or two until the closing marker arrives —
  cosmetic only, self-corrects, not fixed here (would need buffering the
  streamed text until the model emits a stable prefix, out of scope for E1).
- `wrapMode: TextEdit.Wrap` (word-boundary wrap, falls back to
  wrap-anywhere for unbroken tokens like long URLs) — matches the previous
  `Text.WordWrap` behavior plus keeps long paths/links from overflowing the
  bubble.
- Color/selection are Style tokens only: `Style.text` (body), 
  `Style.sectionColor("Chat")` (selection fill — same azure tone as bubble
  accents), `Style.accentText` (selected-text foreground, mirrors the
  precedent in `controls/PmTextField.qml`'s `selectionColor`/
  `selectedTextColor` pair). No hardcoded colors introduced.
- `bubble.width`/`bubble.height` still read `msg.implicitWidth`/
  `msg.implicitHeight` unchanged — `TextEdit` exposes the same
  implicit-size properties as `Text`/`Label`, so the existing bubble-fit-to-
  content sizing logic (line 67-68) needed no changes.

## 2. Gesture arbitration

**Chosen approach: do nothing extra — rely on Qt's built-in
`TextEdit`/`Flickable` grab arbitration, plus the RowLayout's existing 9px
margins as an explicit non-text scroll strip.** No `DragHandler`, no
`pressDelay` tuning, no `ListView.interactive` toggling was added.

Why this is sufficient for the stated target (desktop mouse, per the DAG:
*"Test both on desktop mouse"*):

- `ListView`/`Flickable` is unchanged — still owns wheel events (a
  plain `TextEdit` does not accept `Wheel` events, so they propagate straight
  up to the `ListView`), the `PmScrollBar` (a separate control, never
  contested), and any drag that starts outside the `TextEdit`'s bounds —
  which includes the RowLayout's 9px margins on all sides of the bubble, the
  `Style.gapSm` spacing between bubbles, and the unused horizontal space next
  to narrower-than-82%-width bubbles.
- For drags that *start* on the glyphs themselves: Qt's `TextEdit`/
  `TextInput` mouse-selection code calls `setKeepMouseGrab(true)` once it
  recognizes a genuine text-selection drag is underway (this is the same
  mechanism, already present in Qt 6, that makes selectable text inside a
  `ListView` delegate usable in the first place — see the existing
  `MobileAccessView.qml:196-207` `TextEdit` inside a `Flickable`, same
  pattern, already shipping). Once that grab is held, `Flickable`'s
  `childMouseEventFilter` does not steal the drag, so a selection started on
  text continues to extend even if the pointer moves vertically past the
  original line.
- This satisfies E1's literal acceptance line: *"select+copy text from an old
  message while the list can still be wheel- and drag-scrolled from
  margins."* — wheel: unaffected; margins: unaffected; text: now selectable.

**Documented fallback (not applied, flagging for EV/F2 manual pass):** if a
live/touch run shows the `Flickable` still stealing drags that start on text
(most likely on touch input, where Qt's press-drag heuristics differ from
mouse), the DAG's suggested mitigations are a `DragHandler` scoped to the
RowLayout margin region, or `ListView.pressDelay` tuning. I did not
preemptively add either because (a) the target for this node is explicitly
desktop mouse, (b) I have no way to build/run and empirically confirm a
`DragHandler`-based fix actually helps rather than adding a second gesture
consumer that itself has to be arbitrated, and (c) the existing
`MobileAccessView.qml` precedent (`TextEdit` inside `Flickable`, no extra
arbitration code) suggests the plain approach already works in this codebase's
Qt version. Manual test at F2 item 7 ("Chat: select + copy text; drag-scroll
still works") is the right place to confirm and, if needed, land the
DragHandler fallback as a small fix-forward.

## 3. Context menu

Right-click anywhere on a message body opens a themed `Menu` (three
`MenuItem`s): **Copy** (enabled only when `msg.selectedText.length > 0`),
**Select All**, **Copy Message** (select-all → copy → deselect, so it doesn't
leave a visible full-bubble selection behind). Implementation notes:

- A `MouseArea` with `acceptedButtons: Qt.RightButton` is layered as a child
  of the `TextEdit` (`anchors.fill: parent`). Because it only accepts the
  right button, left-button press/drag events it doesn't accept fall through
  to the `TextEdit` beneath for normal selection — verified by reading Qt's
  documented `MouseArea.acceptedButtons` fall-through behavior; no separate
  code path duplicates left-click handling.
- `Menu`/`MenuItem` come from the already-imported `QtQuick.Controls.Basic`
  (no new import). Since Basic style ships unthemed, `background`/
  `contentItem` are hand-styled per `MenuItem` using existing Style tokens
  only (`Style.surface`, `Style.glassBorder`, `Style.radiusSm`/`radiusXs`,
  `Style.controlHsm`, `Style.sectionColor("Chat")` tint for the highlighted
  row, `Style.text`/`Style.textFaint` for enabled/disabled text) — mirrors the
  existing hand-themed pattern in `controls/PmComboBox.qml`'s popup delegate.
  No new shared `PmMenu` control was introduced since this is the first
  context-menu use in the codebase and E1 owns only `ChatView.qml`; if a
  second context menu shows up in a later node it's worth extracting one, but
  that's out of scope here (file-ownership rule).

## 4. `capture_views` concern

None beyond what's noted above under textFormat. `ChatView` was already
captured pre-E1 (chat delegate binds only pre-existing context properties);
this change adds no new context property, no new invokable, no new model
role — `bubbleRow.text`/`.who`/`.streaming` are unchanged bindings, so no stub
edit is required and no `E1_stubs.md` was written. The only ask of EV is the
visual markdown-rendering eyeball pass called out above.

## Confirmation

- Only existing context properties/models are bound: `app.chatModel`,
  `app.personalities()`, `app.activePersonality`, `app.setPersonality`,
  `app.sendChat` — none of these were touched or extended.
- No new imports added (`QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Layouts`,
  `Polymath` — same four as before).
- Colors/sizes are 100% `Style.*` tokens; no hardcoded hex/px values were
  introduced (the two `implicitWidth: 168` / `leftPadding/rightPadding: 10`
  values on the menu are layout constants, not colors — consistent with
  similar small literal paddings already used elsewhere in this file, e.g.
  `anchors.margins: 9` on the existing RowLayout, `16` for the caret-visible
  width nudge on line 67).
