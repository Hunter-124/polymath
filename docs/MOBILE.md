# Polymath Mobile & Web App

One codebase in [`app/`](../app) ships as the **iOS app**, the **Android app**,
and an installable **mobile web view (PWA)** — built with React + TypeScript +
Vite, wrapped natively by [Capacitor](https://capacitorjs.com). It talks to the
embedded gateway (`src/gateway/`) over the REST + WebSocket contract in
[`app/src/api/contract.ts`](../app/src/api/contract.ts); see [API.md](API.md).

For *how it reaches the home from anywhere*, see [REMOTE_ACCESS.md](REMOTE_ACCESS.md).

## Why this stack

* **One artifact, three targets.** The PWA *is* the mobile web view, and the
  native apps are thin Capacitor shells around the same `dist/`. No duplicate UI.
* **Local-first.** On Wi-Fi the app talks straight to the home server; the relay
  is only used when away. Privacy toggles and data stay on the home machine.
* **Authorized-only.** QR pairing issues a per-device token; revoke it from the
  desktop and the device is locked out immediately.

## Layout

```
app/
  index.html              capacitor.config.ts   vite.config.ts (PWA)
  src/
    api/
      contract.ts         # the wire contract — source of truth
      transport.ts        # LAN ⇄ relay failover + base-URL resolution
      auth.ts             # pairing + device-token lifecycle
      client.ts           # typed REST client (api.*)
      socket.ts           # reconnecting WebSocket event stream
      storage.ts          # Capacitor Preferences / localStorage
    components/           # Layout (tab bar), icons, toasts, UI atoms
    screens/              # Chat, Cameras, Tasks, Timeline, More,
                          # Shopping, Reminders, Memory, Personalities,
                          # Settings, Devices, Pair
    state/store.ts        # small Zustand store (session, toasts)
    theme/theme.css       # dark theme mirroring the desktop palette
```

## Run it

```bash
cd app
npm install

# Web (PWA) dev server — proxies /api and /api/v1/events to a gateway on :8765
npm run dev            # http://localhost:5173

# Type-check / production build (outputs dist/, a deployable PWA)
npm run lint
npm run build
npm run preview
```

### Native apps

```bash
npm run build
npm run add:ios        # one-time: creates ios/  (needs Xcode/macOS)
npm run add:android    # one-time: creates android/ (needs Android Studio)
npm run cap:ios        # sync + open Xcode
npm run cap:android    # sync + open Android Studio
```

The mobile **web view** needs no app store: open the dev/prod URL on a phone and
"Add to Home Screen" — it installs as a standalone PWA.

## Conventions (for adding screens)

* All REST goes through the `api` object in `api/client.ts`; all live updates go
  through the `socket` singleton in `api/socket.ts`. Never call `fetch` directly.
* Images that can't carry an auth header (snapshots, thumbnails) use
  `mediaUrl(path)`, which appends the token as `?token=`.
* Use the atoms in `components/ui.tsx` (`Button`, `Card`, `Row`, `Toggle`,
  `EmptyState`, `Loading`, `relativeTime`) and the `Icon` component, styled by
  the classes in `theme/theme.css`, so screens stay consistent.
* Each screen is a named export (e.g. `export function ShoppingScreen()`) routed
  in `App.tsx`.

## Status

Built and verified (typecheck + production build pass). Connecting it to a live
backend needs the gateway wired into `Polymath.exe` — see
[../WIRING.md](../WIRING.md).
