# 04 — Voice Pipeline Rework + Resource Budget

Target machine: i7-9750H (6C/12T), 32 GB RAM, **RTX 2070 Max-Q 8 GB VRAM** (~0.7 GB Windows
baseline). Machine profile: dedicated to Polymath + YouTube + web research.

## 1. Resource budget (the contract every node must respect)

### VRAM (8192 MiB total)
| Consumer | MiB | Residency |
|---|---|---|
| Windows/DWM/browser baseline | ~700–1200 | always (browser video decode adds ~300–500) |
| Fast LLM: Gemma 3n E4B Q4_K_M @ **4096 ctx, KV q8_0** | ~4600 | resident |
| Embedding: embeddinggemma-300M Q8_0 @ 2048 ctx | ~350 | resident |
| whisper base.en (CUDA) | ~150 | **on-demand** (loaded on wake, unloaded after idle) |
| Vision: Gemma 3 4B + mmproj | ~3500 | on-demand, **evict-Fast-then-load** (never co-resident) |
| VramBudget safety margin | 768 | reserved |
| **Steady-state total** | **~5.7–6.2 GB** | **headroom ~1.9–2.4 GB** ✅ |

- **Heavy 27B role: PARKED.** Remove from default fetch; `requestHeavy` path stays in code
  but the role ships unassigned. Deep reasoning → agent-session delegation (05) or the
  idle-time queue on the Fast model.
- Enable **KV-cache quantization**: expose `type_k/type_v = GGML_TYPE_Q8_0` in
  `llama_backend.cpp` context params behind config `llm.kv_quant` (default `q8_0`).
  (~50 % KV memory vs fp16; quality impact negligible at this scale.)
- Fast n_ctx default **8192 → 4096** (`inference_manager.cpp` autodiscovery + models table
  update). Harness context budgeting (03 §2.4) is designed for 4096.

### CPU / RAM idle targets
| State | CPU | Notes |
|---|---|---|
| Idle, mic on (wake armed) | **< 0.7 %** | Silero VAD every 32 ms only; oWW gated (below) |
| Idle, mic off | ~0 % | capture device fully stopped (existing privacy contract — keep) |
| RAM (app total, models mapped) | < 8 GB | 32 GB machine; not the constraint |

## 2. Model set (single source of truth — update `scripts/fetch-models.ps1` + `docs/MODELS.md`)

| Role | Model | Size | Residency |
|---|---|---|---|
| Fast | gemma-3n-E4B-it-Q4_K_M.gguf | ~4 GB | resident, 4k ctx, q8 KV |
| Embedding | embeddinggemma-300M-Q8_0.gguf | ~0.3 GB | resident |
| Vision | gemma-3-4b-it-Q4_K_M + mmproj-f16 | ~3.5 GB | on-demand (cameras kept) |
| Heavy | — (parked; `-Heavy` flag re-adds 27B for capable machines) | — | — |
| ASR | whisper ggml-base.en (+ tiny.en only if ambient enabled) | 140/75 MB | on-demand GPU |
| TTS | Piper en_US-amy-medium (+alan) | small | persistent CPU process |
| Wake/VAD | openWakeWord trio (hey_jarvis) + silero_vad | tiny | CPU resident |

`docs/MODELS.md` is stale (Qwen-era) — rewrite to match this table (node A0).

## 3. Audio pipeline rework (`src/audio/*` — all changes in this section are node A4)

### 3.1 Idle chain inversion (biggest win)
Today: openWakeWord 3-model chain runs on every 80 ms frame, unconditionally; VAD only
runs *after* wake. Invert it:
- **Silero VAD always-on** (512-sample/32 ms, CPU, ~0.3 %) as the first gate.
- **oWW mel→embedding→classifier runs only during VAD speech** (+ trailing 640 ms
  hangover so mid-phrase context isn't lost; keep feeding oWW the buffered pre-roll from
  speech start so the phrase prefix isn't dropped).
- Command/ambient segmentation logic keeps using the same Silero instance (one model,
  one state; reset recurrent state on mode transitions).

### 3.2 ASR residency
- Do NOT load whisper contexts eagerly in `start()`. Load **base.en (CUDA) on wake**
  (~300 ms, overlapped with the "listening" earcon), keep while interaction is active,
  **unload after `audio.asr_idle_unload_s` (default 90 s)** of no wake/PTT → frees ~150 MiB
  VRAM at idle. PTT press also triggers the preload.
- tiny.en loads only when `ambient_transcription` is enabled (lazy), CPU as today.

### 3.3 Un-block the pump thread
- Pump thread (40 ms) does ONLY: drain ring → VAD → (gated) oWW → segment bookkeeping.
- **ASR jobs** (`whisper_full`) run on a dedicated `QThread` (`AsrWorker`): pump posts the
  captured segment buffer; results come back via queued signal → EventBus utterance.
- **TTS** runs on its own worker: capture ring keeps draining during speech.
- Ring buffer 4 s → **16 s** (`FloatRing` size constant) as belt-and-braces.

### 3.4 TTS: persistent + streaming
- Keep Piper as a subprocess but **persistent**: spawn once (`piper --output_raw` reading
  stdin line-per-utterance), reuse across utterances (kills the per-call espeak-ng cold
  load). Watchdog restarts it on crash/exit.
- **Sentence chunking**: split response text on sentence boundaries; synth+queue chunks so
  first audio lands after the first sentence, not the full reply.
- Playback via a persistent non-blocking miniaudio playback device fed from a queue
  (replaces per-call temp device + 10 ms poll loop).

### 3.5 Barge-in v1 (half-duplex, honest)
While TTS is playing: capture stays live; run VAD+oWW with a raised threshold (+0.1) and an
energy gate calibrated against playback loudness. Wake word or PTT during playback →
**stop TTS immediately** (flush queue) and enter Command state. No AEC in v1 (self-echo
false-wake risk mitigated by the raised threshold; wake phrase "hey jarvis" won't appear in
TTS output). Full WASAPI-loopback AEC = future node, out of scope.

### 3.6 Robustness
- ONNX inference exceptions: exponential-backoff session reload (3 attempts, 1/5/30 s)
  instead of permanent `ready_=false` (wakeword.cpp:187, vad.cpp:112). Publish a Notice on
  final failure.
- Honor `audio.input_device`/`audio.output_device` from settings (02): enumerate via
  miniaudio at device init; empty = default. Device-change → clean restart of capture.
- Replace per-sample `deque` preroll and `vector::erase` hot-path patterns with
  ring/index buffers.

## 4. Acceptance criteria (node A4)
1. Idle (wake armed): process CPU < 1 % sustained over 60 s (measure: `Get-Counter` or
   Task Manager), **0 whisper VRAM** allocated (nvidia-smi).
2. Wake→transcript latency ≤ previous behavior +150 ms (ASR lazy-load overlapped).
3. Speaking a 3-sentence reply: first audio < 1.5 s from generation end; capture ring never
   overflows (add a drop counter + assert in test_audio_e2e).
4. Saying the wake word during TTS playback stops playback and captures the command.
5. All existing test_audio_e2e cases green; new tests: gating (oWW never invoked without
   VAD speech), lazy ASR load/unload timing, TTS chunk ordering.
