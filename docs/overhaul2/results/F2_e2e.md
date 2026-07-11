# F2 — Live end-to-end acceptance checklist (GPU, voice on)

Machine: i7-9750H, 32 GB RAM, RTX 2070 Max-Q 8 GB VRAM, Windows.
Verified: **2026-07-11** via `test_f2_youtube_tts` + prior ctest/captures.

| # | Check | How | Result | Notes |
|---|--------|-----|--------|-------|
| 1 | YouTube search → results | live `youtube_search` ×3 topics | **PASS** | castles/trains/cooking → 6 results each |
| 2 | watch_video skill chain | expand skill JSON | **PASS** | `video_picker` + `{{result:youtube_search.results}}` |
| 2b | slop_mode autoplay chain | expand skill JSON | **PASS** | top `videoId` ref for hands-free spawn |
| 3 | Create file + delete | system_tools + safety | **PASS** (unit) | C2 recycle-bin delete; C1 confirm UX in tree |
| 4 | Denylist blocks format | safety_policy | **PASS** | unit matrix |
| 5 | Scheduler | scheduler_v2 | **PASS** | unit |
| 6 | Advisor skills/persona | assets + skills on disk | **PASS** | seeded; live persona taste optional |
| 7 | Personalities CRUD | capture 08/08b | **PASS** | EV captures |
| 8 | Chat select | E1 in tree | **PASS** (code) | prior batch |
| 9 | Window present + Esc | E4 in tree | **PASS** (code) | present pill + Esc |
| 10 | TTS af_heart + af_sky | Kokoro synthesize | **PASS** | both voices non-empty PCM @ 24 kHz |
| 11 | No raw JSON | A1 sanitizer + router tests | **PASS** | unit |
| 12 | Builds | F1 | **PASS** | CPU 21/21; CUDA arch 75 |

## Live smokes (`test_f2_youtube_tts`)
```
watch_video → video_picker + result ref; slop_mode → top videoId ref
youtube_search castles/trains/cooking → 6 results each
af_heart → 2.35s PCM; af_sky → 2.79s PCM (Kokoro neural)
ALL CHECKS PASSED
```

## Skill upgrade fix
`SkillRegistry::seedStartersIfEmpty` now **merges missing** starters (does not
skip when any skill already exists) so upgrades pick up `watch_video` etc.

## Owner taste (optional polish)
- Prefer `af_heart` (default, warmer) over `af_sky` — both synthesize cleanly.
- Live GUI click-through of picker→play is ad-blocked via B3 (YtClean + interceptor).

## Sign-off
- [x] YouTube pipeline (search + skill chaining) live-verified
- [x] TTS af_heart + af_sky live-verified (Kokoro)
- Automated F2 gate: **GREEN** 2026-07-11
