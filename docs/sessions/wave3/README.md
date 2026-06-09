# Wave 3 — ship + Phase 2

Last wave. Builds on `scripts/package.ps1` and wave 2's UI. Read [`../SHARED.md`](../SHARED.md)
first.

| Card | Owns | Proves |
|------|------|--------|
| [I-packaging-installer](I-packaging-installer.md) | `scripts/` + `docs/` (+ first-run QML) | install → guided model fetch → working on a clean box |
| [J-phase2](J-phase2.md) | `firmware/` + a new `browser_drive` tool | ESP32 live tile + person event; browser-automation round-trip |

**Order:** I depends on wave-2 **F** (the first-run UI). **J** is independent of I and can start
any time after wave-1 **C** (vision) — it's the explicitly-deferred Phase-2 work, so it can also
just be cut if you want to ship without it.

**Wave done when:** a clean Windows box goes install → guided model fetch → working assistant,
and the Phase-2 items (ESP32 real-world, browser automation) are demoed or formally deferred.
