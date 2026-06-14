# Hearth Lab Assistant — guided experiments & legitimized reports

Hearth turns a bench session into a structured, verified record. You run an experiment by talking to it;
it walks the steps, captures each measured value (spoken **or read straight off an instrument**), checks
it against an expected range, re-asks when something looks wrong, and produces a formal `.docx` report at
the end. The goal: organize and legitimize scientific work without slowing it down.

## The flow

```
You:    "Start a lab session for the potassium oxidation experiment."
Hearth: creates a session, lays out the steps, asks for the first value.
You:    "Initial mass is 4.21 grams."   (or it reads the balance over the fabric)
Hearth: verify_lab_step → 4.21 g ∈ [0, 500] ✓  → records it, moves on.
You:    "Peak temperature is 152.8."
Hearth: 152.8 °C is outside the expected [60, 80] for this step — "That's high,
        can you re-check the thermocouple?"   (ok=false → it re-asks)
...
You:    "That's everything."
Hearth: finish_lab_session → renders a formal lab report (.docx) from the verified
        data and links it in the Documents list / mobile app.
```

Live progress streams to the desktop UI, the wall panels, and the mobile **Lab Session** screen via
`lab_step` WebSocket events (`ask` → `verifying` → `verified` / `out_of_range` → `done`).

## How it's built (reusing what already exists)

- **A persona, not a hard-coded flow.** `assets/personalities/lab-guide/persona.json` (`preferred_model:
  heavy`, low temperature) gives the model its instructions and default validation ranges (temp, mass,
  time, pH). The LLM drives the conversation; the tools enforce the bookkeeping.
- **Six agent tools** (`src/agent/tools/`): `start_lab_session`, `next_lab_step`, `verify_lab_step`,
  `finish_lab_session` (the state machine, persisted in the `lab_sessions` / `lab_session_steps` tables so
  a session survives a restart), plus `read_instrument` and `record_measurement`.
- **Range checking.** `verify_lab_step` / `record_measurement` compare a value to the active step's
  expected range first, else the instrument's `expected_min/max`. Out-of-range returns `ok=false`, which
  makes the model re-ask rather than silently accept a bad number.
- **Instrument values.** When a [Hearth Measurement Module](HARDWARE.md#3-lab-instrument-modules--hearth-measurement-module-hmm)
  is on the fabric, the latest reading is already in the `measurements` table; `read_instrument` returns
  it so a value can be captured without anyone typing or speaking it.
- **The report.** `finish_lab_session` gathers the verified steps and hands them to the existing
  `GenerateLabReportTool`, which renders a real Word `.docx` (Title / Objective / Materials / Method /
  Results / Analysis / Conclusion) and back-links `lab_sessions.report_doc_id`.

## Data model

| Table | Purpose |
|---|---|
| `lab_sessions` | one row per experiment (title, objective, status, report_doc_id, started/ended) |
| `lab_session_steps` | the plan + captured/verified value per step (expected kind/unit/min/max, measured value, verified flag) |
| `measurements` | every reading — instrument-pushed (`source='instrument'`) or voice/manual (`source='voice'`), with `in_range` |
| `instruments` | the registry of measurable channels (unit, device_class, expected range) |

See [`SCHEMA.md`](SCHEMA.md) for columns and [`FABRIC.md`](FABRIC.md) §5 for the instrument reading wire
shape.
