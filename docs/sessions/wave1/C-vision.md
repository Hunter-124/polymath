# Wave 1 · Card C — Vision, end-to-end

**Read [`../SHARED.md`](../SHARED.md) first.**  Owns: **`src/vision/` only.**

## Goal
Prove the per-camera pipeline on recorded input and fix gaps:
`decode → motion (MOG2) → person (YOLOv8n) → face (SCRFD detect + ArcFace embed) →
object-find (VLM) → visual memory + events persisted`. ONNX runs on CPU (fine).

## Verify (recorded clip / images — no live camera)
1. **Person** — a frame/short clip with a person → ≥1 `person` detection + a `PersonEvent` + an
   `events` row + a thumbnail under `media/`.
2. **Motion gating** — a static clip fires no heavy stages; a moving clip gates the person/face
   stages on motion.
3. **Face** — enroll a face image into the gallery → another image of the same person →
   `FaceEvent{userId}` matches; a stranger → no match.
4. **Object-find** — with the VLM (Gemma 3 4B + mmproj) loaded, ask "where is the {object}" over
   recent frames → a last-seen answer (camera + timestamp) from visual memory.

## How
- Read `src/vision/*` (camera_worker, motion, detector_yolo, face_arcface, visual_memory,
  finder) and how `VisionService` emits EventBus messages.
- `tests/test_vision_e2e.cpp` with fixtures in `tests/fixtures/vision/` (a person clip, two face
  images, a static clip). Feed frames straight to the worker/detectors — no network camera.

## Done when
`ctest -R vision` passes: person-detect + event, motion gating, face enroll+match+reject, one
find-object answer. Report at `docs/sessions/reports/C-vision.md`.
