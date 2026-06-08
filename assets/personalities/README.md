# Starter personalities

Each subfolder is a **persona bundle**: a `persona.json` (required) plus an
optional `avatar.png`. On first run these are copied into `data/personalities/`;
add your own there (or here) — drop in a folder and the PersonalityManager picks
it up on its next scan. Switch the active persona from the **Personalities** view.

`persona.json` fields:

| field | meaning |
|-------|---------|
| `name` | display name (unique) |
| `system_prompt` | the persona's instructions / character |
| `voice` | Piper voice id (must exist under `models/piper/`) |
| `preferred_model` | `"fast"` or a Model-Manager registry id |
| `wake_phrase` | optional per-persona wake word |
| `sampling` | `temperature`, `top_p`, … |
| `tools` | allow-list of tool names (`[]` = all tools) |
