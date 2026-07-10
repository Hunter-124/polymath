export const meta = {
  name: 'polymath-overhaul',
  description: 'Execute the Polymath overhaul DAG (docs/overhaul/06_DAG.md) wave by wave with parallel subagents',
  whenToUse: 'Run with args {wave:"A"|"B"|"C"|"D"} (or {wave:"all"}) from the polymath repo root after reading docs/overhaul/07_KICKOFF.md',
  phases: [
    { title: 'Wave A', detail: 'foundations: A0∥A1 → A2 → A3∥A4' },
    { title: 'Wave B', detail: '9 parallel GUI packages → BV verify' },
    { title: 'Wave C', detail: 'C2∥C3∥C4 → C5 → C1' },
    { title: 'Wave D', detail: 'build, test, live verify, docs' },
  ],
}

// ---------------------------------------------------------------------------
// Every agent prompt is grounded in the spec pack. The DAG node table in
// docs/overhaul/06_DAG.md is the authority on file ownership and acceptance.
// ---------------------------------------------------------------------------
const ROOT = 'C:/Users/Yakub/Desktop/NEWNEWGEORGE/polymath'
const COMMON = `You are executing one node of the Polymath overhaul DAG.
Repo root: ${ROOT}. FIRST read ${ROOT}/docs/overhaul/06_DAG.md (global rules + your node row)
and ${ROOT}/docs/overhaul/00_MASTER_PLAN.md §3 (decisions), then the spec sections your node
cites. HARD RULES: (1) touch ONLY the files your node owns — if another file seems necessary,
stop and return a report saying so instead of editing; (2) do not run cmake builds unless
your node is marked "Builds"; (3) preserve every behavior/binding listed in
docs/overhaul/01_GUI_DESIGN_SYSTEM.md §0/§5 that applies to your files; (4) when done and
acceptance criteria are met, git add exactly your owned files + docs/overhaul/PROGRESS.md
(tick your node, add commit note) and commit as "overhaul(<node>): <summary>" with
Co-Authored-By: Claude <noreply@anthropic.com>. Return a concise report: what changed,
acceptance status, any deviations.`

const RESULT_SCHEMA = {
  type: 'object',
  properties: {
    node: { type: 'string' },
    status: { enum: ['done', 'blocked', 'partial'] },
    commit: { type: 'string' },
    report: { type: 'string' },
    blockers: { type: 'array', items: { type: 'string' } },
  },
  required: ['node', 'status', 'report'],
}

function nodeAgent(id, extra, opts) {
  return agent(`${COMMON}\n\nYour node: **${id}**. ${extra}`,
    Object.assign({ label: id, schema: RESULT_SCHEMA }, opts || {}))
}

const wave = (args && args.wave) ? String(args.wave).toUpperCase() : 'ALL'
const out = { waves: {} }

// ------------------------------ WAVE A ------------------------------------
if (wave === 'A' || wave === 'ALL') {
  phase('Wave A')
  log('Wave A: A0 ∥ A1, then A2, then A3 ∥ A4')
  const a01 = await parallel([
    () => nodeAgent('A0', 'Spec: docs/overhaul/04_VOICE_RESOURCES.md §2 (model table). Rewrite docs/MODELS.md; edit scripts/fetch-models.ps1 (27B behind -Heavy); fix docs/STATUS.md hardware claims (RTX 2070 Max-Q 8GB, i7-9750H).', { phase: 'Wave A' }),
    () => nodeAgent('A1', 'Spec: docs/overhaul/01_GUI_DESIGN_SYSTEM.md §1–3 + §7. This node BUILDS: verify with the §7 capture loop before committing.', { phase: 'Wave A' }),
  ])
  const a2 = await nodeAgent('A2', 'Spec: docs/overhaul/02_GUI_FEATURES.md (all) + event payloads listed in the A2 row of 06_DAG.md (SurfaceRequest + GoalUpdate + AgentSessionEvent in ONE event_bus edit). Create the placeholder QML files listed in your node row and register them. This node BUILDS (full build of build/cpu) and runs captures.', { phase: 'Wave A' })
  const a34 = await parallel([
    () => nodeAgent('A3', 'Spec: docs/overhaul/03_HARNESS.md §1+§3 and docs/overhaul/04_VOICE_RESOURCES.md §1 (countTokens, n_ctx 4096, KV q8_0). This node BUILDS and runs its deterministic tests via ctest -R agent.', { phase: 'Wave A' }),
    () => nodeAgent('A4', 'Spec: docs/overhaul/04_VOICE_RESOURCES.md §3–4 (entire audio rework). You own src/audio/* and tests/test_audio_e2e.cpp only. Config keys already exist (A2). This node BUILDS and runs ctest -R audio.', { phase: 'Wave A' }),
  ])
  out.waves.A = { a01, a2, a34 }
}

// ------------------------------ WAVE B ------------------------------------
if (wave === 'B' || wave === 'ALL') {
  phase('Wave B')
  log('Wave B: 9 parallel GUI packages (no builds), then BV verify+fix')
  const bSpecs = [
    ['B1', 'Spec: 01 §4 (shell reskin). Feature wiring is NOT yours (C1 does it) — leave clearly-marked hooks.', undefined],
    ['B2', 'Spec: 01 §5.1–5.2 + 02 §Feature 4 (HUD bindings, defensive).', undefined],
    ['B3', 'Spec: 01 §5.3 and §5.5.', 'sonnet'],
    ['B4', 'Spec: 01 §5.4 and §5.6.', 'sonnet'],
    ['B5', 'Spec: 01 §5.7–5.10 (Mobile Access: light restyle, keep white QR exactly).', 'sonnet'],
    ['B6', 'Spec: 02 §Feature 1 (SettingsView QML; controller exists from A2).', undefined],
    ['B7', 'Spec: 02 §Feature 3 (ToastStack, NotificationBell, NotificationCenter QML).', undefined],
    ['B8', 'Spec: 02 §Feature 2 (CommandPalette component only; registry lives in Main.qml, not yours).', undefined],
    ['B9', 'Spec: 02 §Feature 5 (SurfaceHost + surfaces/*).', undefined],
  ]
  const bResults = await parallel(bSpecs.map(([id, extra, model]) => () =>
    nodeAgent(id, extra + ' Do NOT build (BV builds for the whole wave).',
      model ? { phase: 'Wave B', model } : { phase: 'Wave B' })))
  const bv = await nodeAgent('BV', `Verify wave B. Build capture_views in ${ROOT}/build/cpu (Release), run captures (populated + --empty), READ every PNG against 01 §7 checklist. Wave-B agents are finished, so you may fix defects in any wave-B-owned QML file directly. Iterate build→capture→fix until clean, then commit fixes as overhaul(BV).`, { phase: 'Wave B' })
  out.waves.B = { bResults, bv }
}

// ------------------------------ WAVE C ------------------------------------
if (wave === 'C' || wave === 'ALL') {
  phase('Wave C')
  log('Wave C: C2 ∥ C3 ∥ C4 → C5 → C1')
  const c234 = await parallel([
    () => nodeAgent('C2', 'Spec: 03 §2 (AgentLoop v2) + §7 tests. This node BUILDS and runs ctest -R harness.', { phase: 'Wave C' }),
    () => nodeAgent('C3', 'Spec: 03 §4 (skills). Tool CLASSES only — registration belongs to C5. No builds (C5 builds).', { phase: 'Wave C' }),
    () => nodeAgent('C4', 'Spec: 05 (entire agent-sessions system). This node BUILDS (it adds a CMake lib) and runs ctest -R sessions.', { phase: 'Wave C' }),
  ])
  const c5 = await nodeAgent('C5', 'Spec: 06_DAG C5 row: register run_skill/save_skill/agent_*/ui_control in register_tools.cpp, add risk-class metadata (03 §5), implement ui_control tool (02 §Feature 5 schema), browser_drive session-reuse + screenshots. BUILDS + ctest -R agent.', { phase: 'Wave C' })
  const c1 = await nodeAgent('C1', 'Spec: 02 execution-order step 3 (shell integration in Main.qml: palette registry + Ctrl+K, ToastStack swap, bell+center, SurfaceHost overlay, Style↔settings Bindings, Agents/Settings nav live). BUILDS + captures.', { phase: 'Wave C' })
  out.waves.C = { c234, c5, c1 }
}

// ------------------------------ WAVE D ------------------------------------
if (wave === 'D' || wave === 'ALL') {
  phase('Wave D')
  log('Wave D: sequential — full build → tests → live verify → docs')
  const d1 = await nodeAgent('D1', 'Full builds: build/cpu (VS, Release, all targets) and the CUDA tree per docs/BUILD.md + scripts/build-gpu.ps1 conventions. Fix all fallout (you may touch any file, citing each fix in your report). Full capture run must be clean.', { phase: 'Wave D' })
  const d2 = await nodeAgent('D2', 'Run the FULL ctest suite in build/cpu; fix failures (any file, cite fixes). All legacy + new suites green.', { phase: 'Wave D' })
  const d3 = await nodeAgent('D3', 'Spec: 06_DAG D3 row + 04 §1 budget. Fetch models (fetch-models.ps1 base set), run Polymath.exe live, execute every listed live check, record nvidia-smi/CPU measurements into docs/overhaul/results/D3-resource-audit.md. Report any budget violation as blocker.', { phase: 'Wave D' })
  const d4 = await nodeAgent('D4', 'Docs refresh (README, ARCHITECTURE.md, STATUS.md), run `graphify update .`, complete PROGRESS.md, git tag v0.2.0-overhaul.', { phase: 'Wave D' })
  out.waves.D = { d1, d2, d3, d4 }
}

return out
