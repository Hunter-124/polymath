# Wave 1 — every backend subsystem works end-to-end

Turn "services initialize" into "each feature provably round-trips." Five agents, five disjoint
module directories, run together. Read [`../SHARED.md`](../SHARED.md) first.

| Card | Owns | Proves |
|------|------|--------|
| [A-voice](A-voice.md) | `src/audio` | wake→ASR→Utterance, SpeakRequest→Piper, privacy gate |
| [B-agent-tools](B-agent-tools.md) | `src/agent` | all 16 tools + one GBNF tool-call round-trip |
| [C-vision](C-vision.md) | `src/vision` | person/motion/face/object-find on recorded clips |
| [D-tiered-inference](D-tiered-inference.md) | `src/inference` + `src/scheduler` | Fast→Heavy→drain→Fast, VRAM budget, no OOM |
| [E-memory](E-memory.md) | `src/memory` | vector recall, persistence, summarizer, retention |

**Parallel-safe:** the five directories are disjoint; the only shared touch is
`tests/CMakeLists.txt` (append-only) and the frozen contracts (read-only). Cards A/B/E *call*
`src/inference` but **D owns it** — route any inference bug or new generate option to D via
`contract-requests.md`.

**Hardware:** A/C/E run on the CPU build with recorded inputs (fully parallel). D, and the
LLM-in-the-loop steps of B/E, need the GPU — serialize those against each other.

**Wave done when:** all five reports are in `docs/sessions/reports/` and `ctest` is green across
audio / agent / vision / inference / memory.
