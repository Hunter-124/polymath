# Privacy model

Polymath is **local-first by design**: every model runs on your machine, all data
stays on disk under `data/`, and there is **no telemetry or cloud dependency**.
The only outbound traffic is what *you* invoke — the web-search / fetch tools (to
your chosen search backend) and any model downloads.

## Default posture: configurable, default-ON
Per the product decision, the powerful ambient features ship **enabled** with
easy per-feature toggles in the **Privacy / Settings** view. Each maps to a key
in the `settings` table (defaults seeded by `Config::seedDefaults`):

| Toggle | Key | Default |
|--------|-----|---------|
| Microphone | `privacy.mic_enabled` | ON |
| Ambient transcription (daily summaries) | `privacy.ambient_transcription` | ON |
| Face recognition (identify users) | `privacy.face_recognition` | ON |
| Cameras | `privacy.cameras_enabled` | ON |
| Encrypt database at rest | `privacy.encrypt_at_rest` | OFF |

Turning a sense off **stops capture at the source** (the AudioService /
VisionService check the flag and a `PrivacyChanged` event is broadcast on the
EventBus), not just hiding the UI.

## Retention
Ambient data is the most sensitive, so it expires soonest. The MemoryService
retention sweeper deletes rows past their TTL:

| Data | Key | Default |
|------|-----|---------|
| Ambient transcripts | `retention.ambient_days` | 7 days |
| Vision/audio events | `retention.events_days` | 30 days |

`0` means keep forever. Command (post-wake-word) transcripts and explicit
memories are kept until you delete them.

## Identity & enrollment
- Face and voice **enrollment is explicit and per-person** (`users` table +
  galleries under `data/media/`). Nothing identifies a specific person until you
  enroll them; with face recognition off, vision does generic person detection only.
- Galleries are deletable, which removes the ability to recognize that person.

## Encryption at rest
Enabling `privacy.encrypt_at_rest` opens the SQLite database through **SQLCipher**
with a key derived from the OS credential store. Media blobs can be stored in an
encrypted container too. (Off by default to keep first-run friction low.)

## Agent / web tools
Web search, page fetch, and the (phase-2) browser tools are **allow-listed per
personality** (`tools` in `persona.json`) and every call is surfaced in the
activity log, so you can see exactly when the assistant reached the internet.
