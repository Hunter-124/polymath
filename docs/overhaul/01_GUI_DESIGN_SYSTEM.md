# 01 — GUI Design System: "Holographic Aurora"

Spec for the complete visual redesign of the desktop QML UI. Implementing agents: follow
this exactly; where a value is given (hex, px, ms, alpha) it is the decision, not a
suggestion. Keep all public control APIs listed in §0.

## 0. Hard constraints (verified against the tree)

- Build dir `polymath/build/cpu` (VS 2026 generator). `QtQuick.Effects` (`MultiEffect`) and
  `QtQuick.Shapes` are installed in the Qt 6.6.3 kit. `Qt5Compat.GraphicalEffects` is NOT.
- `capture_views.cpp` forces the **Software** scene-graph backend: `ShaderEffect`/
  `ShaderEffectSource`/`MultiEffect` render nothing there. Every hero surface must degrade
  to faux-glass; the aurora wallpaper must be `Rectangle` gradients + `QtQuick.Shapes`
  (QPainter software paths) — never `ShaderEffect`.
- Public APIs to preserve: `PmButton.accent/flat/highlighted/contentItem` (Main.qml overrides
  contentItem), `PmTextField` focus ring + `onAccepted`, `PmComboBox` model/textRole/
  displayText/delegate contract, `PmSwitch`/`PmCheckBox` checked, `PmItemDelegate`
  text/highlighted, `PmToolButton` text, `EmptyState`
  glyph/title/hint/glyphColor/actionText/actionVisible/actionTriggered.
- Context models stay: `app.chatModel` (property), bare `shoppingModel/cameraModel/
  taskModel/timelineModel/gateway`. Role names / `required property` names are load-bearing.
- Glyphs must stay ASCII/Latin-1-safe (`● ○ + ? ! × ▍ ▸ ✓`); all new iconography is vector
  (`PmIcon` via Shapes). No emoji (offscreen renderer has no fallback font).
- All controls keep `import QtQuick.Controls.Basic` as the base (focus/keyboard behavior).

## 1. Token system — new `Style.qml`

Keep `pragma Singleton` and all existing token *names* (re-point values); add new tokens.
Tokens that Settings can override at runtime (`accent`, `fontScale`, `effectsEnabled`,
`effectsIntensity`, `reduceMotion`) must be `property` (writable), not `readonly` —
Main.qml bridges them from the `settings` context property (see 02 §Feature 1).

### 1.1 Base ramp
```
bgDeep      "#04060C"   // absolute window base behind aurora
bg          "#070A12"
surface     "#0E1320"
surface2    "#141B2C"
surface3    "#1B2439"
border      "#26304A"
borderSoft  "#1A2236"
```
### 1.2 Text
```
text "#E7ECFF"   textDim "#9AA6CC"   textFaint "#5B6684"
```
### 1.3 Accents
```
accent "#33E1FF" (writable)  accentBright "#8AF1FF"  accentDim "#0E2E3B"  accentText "#03121A"
good "#43E39A"  warn "#FFC24B"  bad "#FF5C78"  info "#67D3FF"  magenta "#C58CFF"
```
### 1.4 Glass tokens (new)
```
glassFillTop    Qt.rgba(1,1,1,0.065)
glassFillBottom Qt.rgba(1,1,1,0.020)
glassBorder     Qt.rgba(1,1,1,0.10)
glassHighlight  Qt.rgba(1,1,1,0.22)
glassTintAlpha  0.14
glassHoverBoost 0.05
shadowA1 Qt.rgba(0,0,0,0.20)  shadowA2 Qt.rgba(0,0,0,0.10)  shadowA3 Qt.rgba(0,0,0,0.05)
```
### 1.5 Section hues + accessors (new)
```qml
readonly property var sectionHues: ({
  "Dashboard":"#33E1FF", "Chat":"#4C8CFF", "Cameras":"#2BD9C6",
  "Tasks":"#FFB23E", "Timeline":"#9B6BFF", "Shopping":"#46E08A",
  "Personalities":"#FF6BD0", "Models":"#6D7BFF", "Privacy":"#FF5C6E",
  "Mobile Access":"#4FB8FF", "Agents":"#7FE0FF", "Settings":"#A8B4D8"
})
function sectionColor(name) { return sectionHues[name] !== undefined ? sectionHues[name] : accent }
function sectionGlow(name, a) { var c = sectionColor(name); return Qt.rgba(c.r,c.g,c.b, a===undefined?0.35:a) }
function tint(c, a) { return Qt.rgba(c.r,c.g,c.b,a) }
```
### 1.6 Aurora stops (new)
```
auroraBase "#04060C"
auroraBlobA "#33E1FF"  auroraBlobB "#6D7BFF"  auroraBlobC "#2BD9C6"
auroraBlobAlpha 0.22
```
### 1.7 Shape & rhythm
```
radiusLg 16 (new)  radius 12  radiusSm 8  radiusXs 6  radiusPill 999 (new)
gap 12  gapLg 16  gapSm 8 (new)  pad 24  padSm 16 (new)
controlH 36  controlHsm 30 (new)
```
### 1.8 Type ramp
```
fsDisplay 30 (new)  fsTitle 24  fsHeading 18  fsBody 14  fsSmall 12  fsTiny 11
letterSpaceWide 2 (new)
property real fontScale: 1.0 (writable; multiply into fs* via helper or bind at use sites)
```
### 1.9 Motion tokens (new)
```
durFast 120  durBase 200  durSlow 320  durAmbient 26000
property bool animationsEnabled: true
property bool reduceMotion: false      // writable; collapses page/list animations to fades
```
### 1.10 Elevation / icons (new)
```
iconSm 16  iconMd 20  iconLg 24  iconXl 28
elev0/elev1/elev2/elev3 — shadow presets consumed by GlassPanel (see 2.1)
```
### 1.11 `effectsEnabled`
`property bool effectsEnabled: true` + `property real effectsIntensity: 0.6` (both writable).
C++ sets a **`pmEffectsEnabled` context property**: real app `true` (main.cpp),
`capture_views.cpp` `false`. Style keeps a safe `true` default; the entry points push it in:
- `Main.qml` → `Component.onCompleted: Style.effectsEnabled = pmEffectsEnabled`
- capture wrapper Window (string in capture_views.cpp) does the same.

## 2. Glass recipes

### 2.1 `GlassPanel.qml` — faux glass, software-safe (the workhorse)
Item props: `radius=Style.radiusLg`, `tintColor="transparent"`, `tintAlpha=Style.glassTintAlpha`,
`elevation=1`, `hovered=false`. Layers bottom→top:
1. Faux shadow (if elevation>0): 3 stacked rounded Rectangles behind — margins (1,3,7),
   y-offsets (2,5,10), colors shadowA1/A2/A3.
2. Base gradient fill: `Rectangle{radius; gradient: Gradient{ 0: glassFillTop; 1: glassFillBottom }}`.
3. Section tint overlay: `Rectangle{radius; color: Style.tint(tintColor, tintAlpha)}` (skip if transparent).
4. Top inner highlight: 1px Rectangle at top, inset 1px L/R, horizontal gradient
   glassHighlight → transparent at both ends.
5. Hairline border: transparent Rectangle, border 1px `glassBorder` (mix 35 % toward tint when tinted).
6. `default property alias content: contentHolder.data`.
Hover: Behavior bumps fill alpha +glassHoverBoost, border toward sectionGlow(…,0.5).

### 2.2 `GlassCard.qml`
GlassPanel preset: `radius: Style.radius`, `elevation:1`, `property string section` →
`tintColor: Style.sectionColor(section)`, `tintAlpha: 0.10`. Replaces every
`Rectangle{color:Style.surface; border.color:Style.borderSoft}` card.

### 2.3 `GlassSurface.qml` — hero backdrop blur
Props: `sourceItem`, `radius`, `blur=1.0`, `section`.
- effectsEnabled: `ShaderEffectSource` of sourceItem (live, sourceRect mapped) →
  `MultiEffect{ blurEnabled:true; blur; blurMax:40; saturation:0.15; contrast:0.05;
  maskEnabled:true; maskSource: roundedMask }` where roundedMask is a hidden
  `layer.enabled` Item containing a white rounded Rectangle. Overlay GlassPanel layers 3–5
  on top so blurred glass still reads tinted.
- else: render a GlassPanel with the same props (captures always take this path).
Used by: nav rail, titlebar, command palette, PmDialog, PmTooltip, notification popover.

### 2.4 `AuroraBackground.qml` — ambient wallpaper (software-safe)
z:0 behind everything. Base Rectangle `auroraBase`; 3 drifting blobs as `QtQuick.Shapes`
`Shape/ShapePath` ellipses (~700–900 px) with `RadialGradient` from
`tint(blob, auroraBlobAlpha)` center → transparent edge (softness = alpha falloff, no blur
needed). Each blob: slow looping NumberAnimation on centerX/centerY + scale 1.0↔1.12 over
`durAmbient` (26 s), phase-offset, InOutSine, `running: Style.animationsEnabled`.
Faint bottom vignette (transparent → rgba(0,0,0,0.25)).
`property bool useShapes: true` — if the first capture shows Shapes RadialGradient failing
under Software, flip to the fallback: stacked rotated linear-gradient Rectangles.

## 3. Component library

New dirs: `qml/theme/`, `qml/effects/`, all files registered in `src/ui/CMakeLists.txt`
(`qml/theme/Icons.qml` marked `QT_QML_SINGLETON_TYPE TRUE` like Style).

### 3.1 New primitives
- `theme/Icons.qml` (singleton): `readonly property var paths` — 24×24-viewBox SVG path
  strings for: dashboard, chat, camera, tasks, timeline, cart, persona, model, shield,
  phone, settings, search, bell, close, min, max, restore, mic, send, plus, trash,
  chevronLeft, chevronRight, chevronDown, check, live, terminal, globe, play.
- `controls/PmIcon.qml`: Shape+ShapePath rendering `Icons.paths[name]`. Props: `name`,
  `size=Style.iconMd`, `color=Style.text`, `filled=false` (fill vs 1.75px stroke),
  `glow=false` (second faint wider stroke in sectionGlow when effects on).
- `effects/AuroraBackground.qml`, `controls/GlassPanel.qml`, `GlassCard.qml`, `GlassSurface.qml`.

### 3.2 Restyled existing controls (API-compatible; all gain `property color tone: Style.accent`)
- **PmButton**: accent → filled cyan gradient (accent → Qt.darker(accent,1.12)) + accentText
  + subtle glow border; plain → translucent glass fill + glassBorder; flat → transparent,
  hover raises fill alpha. Press: scale 0.98 Behavior 120 ms. contentItem stays overridable.
- **PmTextField**: glass background; focus ring 2px accent/tone + soft outer glow
  (effects-on) / solid ring (off). Keep onAccepted.
- **PmComboBox**: glass field + glass popup; Canvas triangle → `PmIcon "chevronDown"`.
- **PmSwitch**: 44×24 glass track; on = tone gradient + faint glow; thumb 18 px, 120 ms.
- **PmCheckBox**: 20 px glass box; checked = tone fill + PmIcon "check" (keep ✓ fallback).
- **PmItemDelegate**: 44 px; hover/selected = glass fill + 3px left tone bar + sectionGlow
  border when highlighted.
- **PmToolButton**: 30×30 glass-on-hover; destructive hover → bad. Add optional `iconName`.
- **EmptyState**: badge = glass disc + sectionGlow ring; add optional `iconName`; keep all props.

### 3.3 New components
- **PmTooltip**: GlassSurface popup; props text, delay=500, section, optional 6px Shape arrow.
- **PmBadge / PmPill**: tokenized tint idiom. Props text, color=Style.accent, filled=false
  (filled → solid+accentText; else tint 0.16 fill + colored text), radius=radiusPill.
- **PmStatusDot**: props color, size=10, pulsing=false (opacity 1↔0.3 @700 ms loop).
- **PmSectionHeader**: props title, subtitle, section, default alias `actions` (right slot).
  fsTitle bold + small sectionColor underline tick; subtitle textFaint.
- **PmScrollBar**: glass track, tone thumb, auto-hide. Views set `ScrollBar.vertical: PmScrollBar{}`.
- **PmDialog**: glass modal (GlassSurface, elev3, scrim rgba(0,0,0,0.5)); props title,
  default content, footer buttons.
- **PmSlider**: glass groove, tone fill, glowing handle (settings sliders).

## 4. Shell redesign — `Main.qml`

### 4.1 Frameless window
- `flags: Qt.Window | Qt.FramelessWindowHint`; `color:"transparent"`; 1280×820 default.
- Z-order: AuroraBackground z0 → RowLayout (nav + PageHost) z1 → titlebar z2 →
  toasts/tooltips/dialogs/SurfaceHost z3.
- Titlebar (40 px GlassSurface): left wordmark "POLYMATH" (cyan, letterSpaceWide) —
  center command-palette pill ("Search · ask anything", opens palette) — right
  listening PmStatusDot + NotificationBell + window buttons (PmToolButton + PmIcon
  min/max/restore/close; close hovers bad).
- Drag: `DragHandler{ target:null; onActiveChanged: if(active) window.startSystemMove() }`
  on titlebar; double-click toggles maximize. Resize: 8 px edge/corner handlers →
  `window.startSystemResize(edge)`. Maximized: inset content margins; swap max/restore icon.
  (FramelessWindowHint drops OS shadow/snap affordances; startSystemMove still gives snap.)

### 4.2 Nav rail (collapsible, grouped, color-coded)
GlassSurface rail, width animated 72 ↔ 220 (durSlow OutCubic), collapse toggle top.
Groups (captions hidden when collapsed): CORE = Dashboard, Chat · SENSE = Cameras,
Timeline · WORK = Tasks, Shopping, Agents · SYSTEM = Personalities, Models, Privacy,
Mobile Access, Settings. Extend `pages[]` to `{name, src, icon, group}`. Delegate =
PmButton flat with contentItem: PmIcon (active → sectionColor + glow) + label (fades when
collapsed) + 3px left sectionColor bar + sectionGlow halo when active;
`tone: Style.sectionColor(name)`. Collapsed: PmTooltip with page name.
Listening block (top): PmStatusDot pulsing `app.listening` + persona + modelStatus in a
GlassCard. PTT pinned bottom: keep `onPressed/onReleased` press-and-hold semantics,
held = mic icon + "Listening…" + pulsing glow.

### 4.3 Page transitions
Replace StackLayout with **PageHost** Item: Repeater of always-`active` Loaders (preserves
Component.onCompleted refreshes + timers), each anchors.fill. On currentIndex change:
incoming opacity 0→1 + y 12→0 (durBase OutCubic); outgoing opacity→0 (durFast).
`reduceMotion` → crossfade only.

### 4.4 Toast
Handled by ToastStack (see 02 §Feature 3); keep `app.onNoticePosted` contract.

## 5. Per-view restyle checklist

Every view: outer cards → `GlassCard{section:"<Name>"}`; title block → PmSectionHeader;
lists get PmScrollBar; keep all Component.onCompleted refreshes, required-property names,
models, Connections handler names, and state-bound animations.

1. **Dashboard** (cyan): stat cards → GlassCards + PmIcon; PmStatusDot pulsing; cold-start
   banner → warn-tinted GlassCard. Keep app.hasModels/listening/modelStatus/activePersonality.
   (HUD additions: see 02 §Feature 4.)
2. **Chat** (azure): bubbles — mine = azure glass gradient right; assistant = neutral glass
   left; streaming caret ▍ keeps blink (+faint shimmer only when effectsEnabled). Keep
   app.chatModel, positionViewAtEnd on count change, Enter-to-send, who/text/streaming.
3. **Cameras** (teal): tiles → GlassCards; live = teal PmStatusDot + sectionGlow border;
   badges → PmBadge; keep `#0b0d12` image well, 1 Hz refreshTick Timer,
   `image://cameras/<id>?t=` source, onFindObjectAnswered.
4. **Tasks** (amber): rows → glass; status pill → PmBadge(statusColor); running →
   PmStatusDot pulsing. Keep statusColor() mapping + props.
5. **Timeline** (violet): rows → glass with recolored 3px category rail; category → PmBadge.
   Keep 250 ms debounce → setFilter, filter binding, empty-state search branch.
6. **Shopping** (green): glass rows; PmCheckBox/PmToolButton(trash). Keep add/setDone/
   removeItem/clearDone, strikeout-on-done.
7. **Personalities** (magenta): PmItemDelegates (magenta tone); "● active" → PmBadge(good).
   Keep refresh-on-activePersonalityChanged, setPersonality.
8. **Models** (indigo): toolbar reskin; rows → GlassCard + role stripe; "resident" →
   PmBadge(good, filled). Keep reload(), onModelsChanged (ignoreUnknownSignals),
   setModelRole, openModelsFolder, modelData fields.
9. **Privacy** (rose): banner → tinted GlassCard; toggle rows glass + PmSwitch(rose tone).
   Keep firstRun/privacy/setPrivacy/completeFirstRun + 5 key names.
10. **Mobile Access** (sky, parked feature — light restyle only): cards → GlassCards;
    **keep the white QR Rectangle+Canvas exactly** (scannability), monospace payload,
    gateway.* calls + Connections.

## 6. Motion language
Hover: scale 1.015 + fill +glassHoverBoost + border sectionGlow(…,0.5), durFast.
Press: scale 0.98. Focus: ring 1→2 px + outer glow (effects-on).
Page: fade + translateY 12, durBase. List appear: delegate opacity 0→1 + y 8→0,
`delay: Math.min(index*30, 240)`. Ambient: aurora 26 s loops. All Behaviors/NumberAnimations,
no per-frame JS. `reduceMotion` collapses page/list motion to fades.

## 7. Verification loop (per package)

```powershell
cmake --build "C:/Users/Yakub/Desktop/NEWNEWGEORGE/polymath/build/cpu" --config Release --target capture_views
cd C:/Users/Yakub/Desktop/NEWNEWGEORGE/polymath/build/cpu/bin/Release
$env:QT_QPA_PLATFORM='offscreen'; ./capture_views.exe ./_shots
$env:QT_QPA_PLATFORM='offscreen'; ./capture_views.exe ./_shots --empty
```
Read the PNGs (01-main-shell … 11-mobile-access, plus -empty). Check: aurora visible
(else flip useShapes), glass fill+highlight+border+shadow present, correct section tints,
no tofu, AA-legible text, nothing clipped at 1040×760 / 1280×820, focus rings, empty
states centered, no blank view (= QML load error; check stderr). Effects are OFF in
captures — faux-glass must carry the design. GPU/blur path verified only in a live run.
