# voice-sat — Hearth voice satellite

ESP32-S3 voice satellite: on-device wake gate (**microWakeWord**) → stream
**16 kHz mono** audio to the hub over **UDP :8770** with the FABRIC.md §8 4-byte
header → play TTS replies from `cmd/tts`. Posts wake events to `hearth/<id>/wake`.
Reuses `firmware/common` for Wi-Fi/provisioning, MQTT, identity, auth, OTA.

## Datagram (FABRIC.md §8)
```
[ uint16 device_seq LE ][ uint8 codec (0=pcm16,1=opus) ][ uint8 room_id ] payload
```
`payload` is 20 ms frames (320 samples @16 kHz). The hub's `NetworkAudioSource`
decodes into its `FloatRing`; `Utterance.source = device_id`, so the TTS reply
routes back here via `cmd/tts`.

## Hardware variants (build flags)
| Env | Board / audio | I2S pins |
|---|---|---|
| `bundled_mic` (default) | ICS-43434 MEMS mic + MAX98357A amp | `audio_pins.h` |
| `respeaker` | Seeed ReSpeaker Lite | `audio_pins.h` |
| `xvf3800` | XMOS XVF3800 far-field dev kit (AEC) | `audio_pins.h` |

## Wake word & codec
- **Wake (default build):** microWakeWord's `.tflite` is **not** bundled, so
  `WakeWord` runs an **energy-gate stub** (opens on loud speech). Drop the model +
  TFLite-Micro and define `HEARTH_HAVE_MWW` to enable the real phrase gate. See
  `wakeword.cpp` `TODO(model)`.
- **Codec (default):** **PCM16** (codec byte 0) — always built, the FABRIC.md
  fallback. Opus (codec byte 1) is the preferred default per §8: set
  `HEARTH_CODEC_OPUS 1` in `config.h`, add an Opus encoder lib + `-DHEARTH_HAVE_OPUS`,
  and fill the `TODO(codec)` in `main.cpp`. Until then the device honestly streams
  PCM16.

## Configure / build / flash
```
cp config.example.h config.h        # done in-repo
# edit name/location, HEARTH_HUB_AUDIO_HOST, HEARTH_ROOM_ID
pip install platformio
pio run -e bundled_mic              # or -e respeaker / -e xvf3800
pio run -e bundled_mic -t upload
pio device monitor
```
Board `esp32-s3-devkitc-1`, partitions `partitions.csv`.

## Pairing
Same SoftAP flow as the cameras: AP `Hearth-Setup-<hex6>` → `POST /provision` →
joins Wi-Fi; `device_id` is `hearth-mic-<hex6>`. (Satellites have no media HTTP
API beyond status; they use MQTT + the UDP audio plane.)

## Verify without hardware
`pio run -e <env>` compiles. Audio path can be exercised by pointing the hub's
`NetworkAudioSource` listener at UDP :8770 and sending a datagram with the §8
header (the firmware emits exactly that framing).
