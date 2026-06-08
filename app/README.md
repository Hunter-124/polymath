# Polymath app

The Polymath mobile + web client — a single React + TypeScript (Vite) bundle that
runs as an installable PWA in the browser and is wrapped by
[Capacitor](https://capacitorjs.com) for the native iOS and Android apps.

It talks to your home assistant over the shared REST + WebSocket contract in
`src/api/contract.ts`, reaching the gateway directly on the LAN or through the
relay when you're away from home. Pair once by scanning the QR shown in the
desktop app's **Settings ▸ Mobile Access** screen.

## Prerequisites

- Node.js 18+ and npm
- A running Polymath gateway to talk to (for live data). In development the dev
  server proxies API/WS calls to a gateway on **`http://localhost:8765`** — see
  the `server.proxy` block in `vite.config.ts`. Point that target elsewhere if
  your gateway runs on another host/port.

## Run on the web (development)

```sh
npm install
npm run dev
```

This starts Vite on <http://localhost:5173> with hot-reload. Requests to `/api`
(REST) and the `/api/v1/events` WebSocket are proxied to the gateway on
`:8765`, so the app behaves as if served from the gateway itself.

To pair against a real device in dev, open the desktop app's pairing QR and use
**Enter code manually** (paste the JSON under the QR) if the laptop camera isn't
handy.

## Production build

```sh
npm run build      # tsc -b && vite build  → dist/
npm run preview    # serve the built dist/ locally to sanity-check
```

`npm run build` also emits the PWA service worker + manifest (via
`vite-plugin-pwa`), so the `dist/` output is directly installable
("Add to Home Screen"). Drop the app icons into `public/icons/` first — see
`public/icons/README.md`.

## Native apps (Capacitor)

The same web build is shipped inside the native shells. Add a platform once, then
sync the latest web build into it whenever you rebuild:

```sh
npm run add:ios          # cap add ios       (first time only)
npm run add:android      # cap add android   (first time only)

npm run build            # produce dist/
npm run cap:sync         # cap sync          → copy web assets into ios/ & android/
```

Convenience scripts that sync **and** open the native IDE:

```sh
npm run cap:ios          # cap sync ios && cap open ios       (needs Xcode, macOS)
npm run cap:android      # cap sync android && cap open android (needs Android Studio)
```

From there, build/run on a simulator or device from Xcode / Android Studio as
usual. Re-run `npm run build && npm run cap:sync` after any web change.

## Project layout

```
public/            static assets (favicon.svg, icons/)
src/
  api/             REST client, WebSocket, auth/pairing, transport, the contract
  components/      shared UI atoms, app chrome (Layout), Icon set, Toasts
  screens/         one component per route (Chat, Cameras, Tasks, …)
  state/           Zustand store (session/connection state + toasts)
  theme/           global dark theme CSS
```

## Conventions

- **REST only via the `api` object** in `src/api/client.ts`; **live updates only
  via the `socket` singleton** in `src/api/socket.ts`. Never call `fetch`
  directly. For `<img>`/`<video>` URLs that can't carry an auth header, use
  `mediaUrl(path)` from the client.
- Styling uses the classes in `src/theme/theme.css` and the atoms in
  `src/components/ui.tsx` for a consistent dark, phone-first look.
- TypeScript runs in strict mode with `noUnusedLocals` / `noUnusedParameters`.
- Privacy-gated screens read `useApp(s => s.status)?.privacy` before showing
  sensitive data (e.g. Cameras checks `privacy.cameras_enabled`).
