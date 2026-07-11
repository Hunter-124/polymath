# F2 — Live end-to-end acceptance checklist (GPU, voice on)

Record results here. **Owner sign-off required for items 1–2 (YouTube demo) and 9 (TTS voice).**

Machine: i7-9750H, 32 GB RAM, RTX 2070 Max-Q 8 GB VRAM, Windows.

| # | Check | How | Result | Notes |
|---|--------|-----|--------|-------|
| 1 | YouTube picker → click → ad-free | "open a youtube video about castles" (×3 topics) | **OWNER** | B4 skill → video_picker via `{{result:youtube_search.results}}` |
| 2 | Slop mode autoplays | "slop mode" | **OWNER** | Chains top search result; curated fallback removed |
| 3 | Create file + delete → Recycle Bin | "create notes.txt on desktop…"; "delete it" | pending live | C2 + C1 confirm dialog |
| 4 | Denylist blocks format | try format via agent | unit: safety_policy green | chat-visible denial |
| 5 | Schedule morning brief | "every day at 8 brief me" | unit: scheduler_v2 green | Tasks Scheduled section |
| 6 | Advisor briefing / screen / sessions | activate Advisor persona | pending live | D3 assets seeded |
| 7 | Personalities CRUD | create/edit/delete in GUI | capture: 08 / 08b OK | E2 |
| 8 | Chat select + drag-scroll | select text; drag list | pending live | E1 |
| 9 | Window present + Esc | "take over the screen…" | **OWNER** for polish | E4 present/Esc/pill |
| 10 | TTS voice A/B | af_heart vs af_sky | **OWNER** | D4 defaults af_heart |
| 11 | No raw JSON in chat | any tool session | pending live | A1 B-LEAK |
| 12 | Idle VRAM budget | after idle | pending live | see 04_VOICE_RESOURCES |

## Automated pre-checks (this session)
- Serial `ctest -j1`: **21/21** green (CPU).
- `capture_views`: **19/19** PNGs written under `build/cpu/captures_overhaul2/`.
- Tool registry: **42** tools (incl. system, screen, orchestration).
- SafetyPolicy unit matrix + system_tools sandbox green.

## Owner sign-off
- [ ] YouTube demo (items 1–2) — voice/watch quality acceptable
- [ ] TTS voice (item 9/10) — af_heart preferred or change default
- Signed: _______________  Date: _______________
