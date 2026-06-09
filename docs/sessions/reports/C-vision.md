# Card C — Vision, end-to-end — Report

**Status: PASS.** `ctest -R vision` green (1/1); full suite green (3/3: core, tools, vision).
CPU build (`scripts/build-cpu.ps1`) stays green. ONNX ran on CPU as specified; no live
camera used (recorded fixtures only).

## Verified (all four card behaviours, with real models on CPU)

Driven by `tests/test_vision_e2e.cpp` against fixtures in `tests/fixtures/vision/`, loading
the same ONNX/GGUF models the app uses (junctioned `data/models`).

1. **Person → detection + event + thumbnail.** YOLOv8n on `person_messi.jpg` returns **5
   person boxes** (normalized, label `person`). A `Detection` is published on
   `EventBus::detection` (the "PersonEvent" — there is no separate `PersonEvent` type; the
   contract models it as a `Detection` with person boxes), an `events` row of kind `person`
   is written, and a JPEG thumbnail is written under `media/events/`. All asserted.
2. **Motion gating.** A static clip (a repeated still) fires **no** motion after the 15-frame
   MOG2 warmup → the heavy YOLO/face stages are gated off. A moving clip (16 frames read from
   `people_walking.avi`; synthetic moving-box fallback if the AVI can't be decoded) fires
   motion → the person stage is gated on. Both asserted.
3. **Face enroll + match + reject.** Enroll a face from `face_a.jpg` (SCRFD detect → ArcFace
   embed → gallery file round-tripped through `saveGallery`/`loadGallery` → `users` row).
   A second view of the same person (synthesised from `face_a` with a mild brightness/scale
   jitter — we have only one real photo of that identity) matches with **cosine 0.989**
   → `FaceMatch{user_id=42}`. The stranger `face_stranger.jpg` (a genuinely different person)
   scores **0.000** → `user_id == -1` (rejected). Both asserted.
4. **Object-find (VLM).** Plumbing is asserted in the always-on suite: empty `VisualMemory`
   → "No recent camera frames to search."; a populated memory with no Vision model →
   `describeImage()` returns "" → Finder yields a well-formed "couldn't find" answer.
   The **real VLM** path is opt-in (`POLYMATH_VISION_VLM=1`) and was run manually: the
   Gemma-3-4B + mmproj VLM loaded, decoded the frame, and produced a correct last-seen
   answer — *"Last seen on Living Room 2 minute(s) ago — on the grass, near Lionel Messi."*
   **The VLM did not crash** (the Finder uses a plain, non-grammar-constrained prompt, so the
   grammar-decode crash flagged in the card does not affect this path). It is env-gated only
   because CPU VLM decode takes ~2.5 min — too slow for the always-on suite, not because it's
   broken.

## Broken → Fixed

**`src/vision/face_arcface.cpp` — SCRFD output tensors were selected by the wrong index, so
face recognition was completely non-functional.** The code assumed the 9 SCRFD outputs are
interleaved per stride as `(score, bbox, kps)` triples (`outs[s*group + 0/1/2]`). The actual
InsightFace `scrfd_500m.onnx` export groups outputs **by type**: outputs 0–2 are the scores
for strides 8/16/32, 3–5 the bboxes, 6–8 the kps (confirmed via the model's graph:
`[12800,1][3200,1][800,1] [12800,4][3200,4][800,4] [12800,10][3200,10][800,10]`). The old
indexing read bbox-regression tensors as scores, so `detect()` returned **646 / 634 "faces"**
on single-face images and embeds came from garbage crops → same-person and stranger both
"matched" (0.626 / 0.648), i.e. recognition was useless and would never reject a stranger.

Fix: select each stride's score/bbox/kps tensor by matching the output's **row count**
(`fw*fh*num_anchors`) and **last-dim width** (1 / 4 / 10), instead of a fixed interleave
offset. This is robust to either export ordering (grouped or interleaved) and to output-name
permutations. After the fix: **1 face** per image, same-person 0.989, stranger 0.000
(rejected). This is the central correctness fix of the card.

## Changed files (and why)

- `src/vision/face_arcface.cpp` — fix SCRFD output-tensor selection (the bug above). Owned
  directory; no signature/contract change.
- `tests/test_vision_e2e.cpp` — **new** integration test covering all four behaviours
  (unbuffered stdout so a failing assert still prints its diagnostic).
- `tests/CMakeLists.txt` — **append-only** block: register the `vision` ctest (links
  `pm_vision pm_inference pm_core Qt6::Core OpenCV onnxruntime`; passes fixture + model dirs
  as compile defs). No existing lines changed.
- `tests/fixtures/vision/` — fixtures committed (were untracked): `person_messi.jpg` (person
  for YOLO), `face_a.jpg` (enroll/same-person), `face_stranger.jpg` (stranger),
  `people_walking.avi` (moving clip). The same-person "second view" is synthesised in-test
  from `face_a` (no second real photo of that identity available).
- `docs/sessions/reports/C-vision.md` — this report.

## Residual gaps

- **Only one real photo per enrolled identity.** The same-person match uses an in-test
  photometric/geometric jitter of `face_a` as the "second view". This genuinely exercises
  ArcFace embedding stability + the match/reject decision, but a second independent photo of
  the same person would be a stronger fixture. Drop a `face_b.jpg` (same identity as
  `face_a`) into `tests/fixtures/vision/` and the test will use it as-is.
- **VLM find-object is opt-in (`POLYMATH_VISION_VLM=1`), not in the default suite.** Verified
  manually (correct answer, no crash) but excluded from `ctest` because CPU VLM decode is
  ~2.5 min. On the GPU build it would be fast enough to include by default.
- **SCRFD detection threshold / NMS not tuned for crowds.** `det_threshold_=0.5`,
  `match_threshold_=0.35` work well on the fixtures; not stress-tested on multi-face frames.
- **Face stage on the live worker path** (`CameraWorker::processFrame`) was not exercised
  against a real RTSP/MJPEG stream (no live camera, per the rules); the detector/embedder/
  match units it calls are all covered directly by the test.

## Contract requests

**None.** All work stayed inside `src/vision/`; the EventBus and schema were used as-is
(`Detection` for person/face events, `events`/`users`/`cameras` tables, `FindObjectResult`).
The test registration used the append-only block in `tests/CMakeLists.txt`.
