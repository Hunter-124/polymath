# Hearth Wall Panels — kiosk deployment

Hearth runs on in-wall / on-counter touchscreens two ways. Pick per panel by what the hardware can run.

## Native Qt panel (`Hearth.exe --panel`)

On a capable Linux SBC (Raspberry Pi 4/5, Seeed reTerminal) the **real Qt6 UI** runs in a fullscreen,
touch-first kiosk dashboard — pixel-parity with the hub, no PWA feature gaps. It shows the current tasks,
a live camera strip, and a streaming chat panel with a large push-to-talk target.

- QML: [`src/ui/qml/PanelMode.qml`](../src/ui/qml/PanelMode.qml) — reuses the `Nav` singleton, the
  `AppController` context properties (`app`, `chatModel`, `taskModel`, `cameraModel`), and the same dark
  palette as the desktop shell. All hit targets ≥ 56 px.
- Launch: `Hearth.exe --panel` (a `QCommandLineParser` flag in `src/app/main.cpp` swaps the root QML from
  `Main.qml` to `PanelMode.qml`; default behavior is unchanged).
- On a Pi, build the UI via **Boot to Qt** ([doc.qt.io](https://doc.qt.io/Boot2Qt/b2qt-qsg-raspberry.html))
  and autostart `Hearth --panel` fullscreen. Recommended: **Pi 5 (4 GB) + Pi Touch Display 2 (7″)**.

## PWA panel (Android tablets / PoE in-wall panels)

Everything that isn't a Qt-capable SBC loads the **React PWA** ([`app/`](../app)) fullscreen via a kiosk
browser. Because most wall surfaces are PWA-only, the app is kept feature-equal to the hub (chat, cameras,
tasks, lab sessions, shopping, settings).

- **Budget:** refurb Amazon Fire HD 10 + **Fully Kiosk Browser** pointed at the app URL.
- **Premium:** PoE in-wall Android panel (RK3566, Android 11) — single cable for power + data, flush mount.
- Pair the panel to the hub like any device (scan the desktop's QR in Settings ▸ Mobile Access). The PWA
  reaches the hub over LAN, or the relay when off-site.

ESP32 displays (ESP32-S3-BOX-3, M5Stack) are **voice / quick-control satellites**, not dashboard panels —
see [`HARDWARE.md`](HARDWARE.md#4-wall-panels).
