# Polymath — parallel build plan (session waves)

Polymath compiles, links, boots, and runs GPU inference. What remains is turning
"boots + initializes" into "works + polished + shippable." That work is split into **waves of
parallel agent sessions**. Each *card* is a self-contained brief one agent runs in its own git
worktree; cards in the same wave own **disjoint directories** so they never collide.

**Every agent reads [`SHARED.md`](SHARED.md) first** — frozen contracts, how to build/run,
rules of engagement, and the definition-of-done. Needed contract changes go in
[`contract-requests.md`](contract-requests.md), never into the frozen files.

## The waves

| Wave | Theme | Cards | Parallel? |
|------|-------|-------|-----------|
| **[wave1](wave1/)** | Every backend subsystem works end-to-end | A voice · B agent-tools · C vision · D tiered-inference+scheduler · E memory | Yes — 5 disjoint module dirs |
| **[wave2](wave2/)** | Whole-system quality | F GUI/UX · G privacy+persistence+security · H integration harness+CI | Mostly — F is the display-bound long pole |
| **[wave3](wave3/)** | Ship + Phase 2 | I packaging+installer+first-run · J ESP32 real-world + browser automation | After wave 2 (I needs F's UI) |

**Dependency order: wave1 → wave2 → wave3.** Within wave 1 all five run together. Wave 2 starts
once wave 1's subsystems are green (G and H consume wave-1 results; F can start anytime but is
the long pole). Wave 3 ships what waves 1–2 produced.

## The one hard constraint
Code edits parallelize; **live verification does not** — one GPU, one mic, one display. Every
card uses recorded/synthetic inputs and the CPU build to dodge this. The only steps that must
serialize on the GPU are the LLM-in-the-loop checks (wave1 B/D/E) and the real-display render in
wave2 F.

## Launching a wave
```powershell
# 1. commit this plan so worktrees include the cards
git add docs/sessions
git -c user.email=local@polymath -c user.name=Polymath commit -m "Session plan: waves 1-3"

# 2. one worktree + branch per card
git worktree add -b wave1/A-voice ..\pm-wave1-A
# junction the ~28 GB models in (don't re-download):
New-Item -ItemType Junction -Path ..\pm-wave1-A\build\cpu\bin\Release\data -Target .\build\cpu\bin\Release\data

# 3. start a session in that worktree with the kickoff prompt:
#    "Execute docs/sessions/wave1/A-voice.md"
```
Or point `/batch` at a wave folder's card files. Each agent leaves a report in
`docs/sessions/reports/`.
