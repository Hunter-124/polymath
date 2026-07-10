# D3 results (2026-07-10)

## Models fetched (Minimal set)
Root: `data/models` (junctioned to `build/cpu/bin/Release/data/models`)

| Asset | Size |
|-------|------|
| gemma-3n-E4B-it-Q4_K_M.gguf (Fast) | ~4.3 GB |
| embeddinggemma-300M-Q8_0.gguf | ~318 MB |
| whisper base.en + tiny.en | ~215 MB |
| Piper amy + alan voices | ~120 MB |
| silero VAD + oWW hey_jarvis trio | ~5 MB |
| yolov8n + SCRFD + ArcFace | ~195 MB |

Not fetched in Minimal: VLM (Gemma 3 4B), Heavy 27B, piper-engine binary (TTS needs separate piper.exe drop-in).

## Live / e2e checks
| Check | Result |
|-------|--------|
| test_agent_e2e + POLYMATH_E2E_LLM=1 | **OK** — shopping tool LLM round-trip |
| ctest 14/14 (model-free) | **OK** (D2) |
| captures 13/13 | **OK** (D1) |
| Full voice wake->TTS | **Deferred** — piper.exe not in tree; mic live loop is interactive |
| agent_spawn live claude | **SKIP** unless claude on PATH |

## Resource snapshot (idle, before load)
```
name, memory.total [MiB], memory.used [MiB], utilization.gpu [%]
NVIDIA GeForce RTX 2070 with Max-Q Design, 8192 MiB, 819 MiB, 8 %

```
Target budget (04 §1): Fast@4k q8 ~4.6 GB VRAM resident. Full nvidia-smi under Fast load should be re-checked on GPU build (build/cpu uses CPU backend for llama in this tree).

## Script fix
`scripts/fetch-models.ps1`: ASCII-safe rewrite + robust Root when PSScriptRoot empty.
