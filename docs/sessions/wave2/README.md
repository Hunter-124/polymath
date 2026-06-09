# Wave 2 — whole-system quality

Starts once wave 1's subsystems are green. Three agents, mostly parallel (F is the long pole —
it needs the real display). Read [`../SHARED.md`](../SHARED.md) first.

| Card | Owns | Proves |
|------|------|--------|
| [F-gui-ux](F-gui-ux.md) | `src/ui` | the 10 views render cleanly, themed, with a real first-run flow |
| [G-privacy-persistence](G-privacy-persistence.md) | `src/core` (db/settings/privacy) | toggles gate capture, retention purges, at-rest encryption |
| [H-integration-ci](H-integration-ci.md) | `tests/` + `scripts/ci` | a real integration suite + CI, green |

**Parallel-safe:** disjoint dirs (`src/ui`, `src/core`, `tests/`). F needs a display so it
serializes on that; G and H are CPU-only and fully parallel. H consumes wave-1's per-module
tests.

**Why after wave 1:** G verifies gating across services that wave 1 stabilized; H wires up wave
1's tests; F is most useful once the views' view-models actually populate.

**Wave done when:** screenshots show a coherent, themed UI with a sane cold-start flow; privacy
gating + retention + at-rest encryption are verified; and a CI run is green from a clean build.
