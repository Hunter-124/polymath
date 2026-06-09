# Wave 2 · Card F — GUI / UX audit + polish

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/ui/` only** (QML + view-model glue).

## Goal
Make the 10 QML views actually look and flow like a product. The shell is solid (sidebar nav,
dark Tokyo-Night palette, streaming chat bubbles, toasts) but it has **never been visually
verified** and uses `QtQuick.Controls.Basic` (minimally styled — controls don't inherit the
container theme).

## Do
1. **Render it.** Launch on a real display (computer-use, or a dev with a screen) and screenshot
   all 9 views. This needs the display — it's the serial long pole; budget for it.
2. **Theme the controls.** Give Button / ComboBox / TextField / etc. one cohesive dark style
   (Basic won't inherit the colors). Fix `QFontDatabase: Cannot find font directory` by bundling
   a font (e.g. Inter / DejaVu) and setting it app-wide.
3. **Flesh + bind every view.** Confirm all 9 populate from their view-models and have sensible
   **empty / loading / error** states (no-models, no-cameras, no-transcripts, model loading…).
4. **First-run flow.** Design the cold start: no models → guide to `fetch-models`; Model Manager
   role assignment; Privacy first-run opt-in; an obvious listening/idle affordance.

## How
- Read `src/ui/qml/*` (Main shell + the 9 views) and the C++ view-models / `AppController`
  surface (`app.*` properties, models, invokables). Keep changes inside `src/ui`.

## Done when
Screenshots of all 9 views are coherent and consistent; a click-through of the core flows (chat,
switch persona, model manager, privacy toggles, push-to-talk) works. Report + before/after
screenshots at `docs/sessions/reports/F-gui.md`.
