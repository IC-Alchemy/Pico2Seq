# Pico2Seq → Windows Desktop App (branch `x86`)

**Date:** 2026-09-17 · **Status:** approved, executing · **Branch:** `x86` (== `DeCluttered` @ `37ecb9a` at start)

**Decisions:** WinUI 3 C# GUI + native C++ engine DLL; **virtual instrument replica** UI (on-screen emulation of the physical device running the firmware's real UI logic).

**Key fact:** the musical brain (`src\pico2seq-core\`, `src\voice\`, rpdsp submodule, `ControlSurfaceLogic`, persistence codecs) already compiles on the host and passes 315 Catch2 tests. The port is adapter work, not a rewrite.

## Guiding principles

1. **Zero fork of musical logic.** The DLL compiles the *same* `src/` files the firmware uses. Platform seams are replaced by host shims under `desktop/native/shims/` — the pattern `tests/stubs/` already proves (stub include dir shadows `Arduino.h`, `Wire.h`, etc.).
2. **Firmware untouched.** Arduino build and the existing test suite stay green; new code lives under `desktop/`. Any firmware-source compile fix gets flagged first (CLAUDE.md forbids Arduino includes in `pico2seq-core` — shims keep that true).
3. **Threading map:** Core 0 control loop → dedicated 1 kHz `std::thread` in the DLL; Core 1 audio → WASAPI callback; existing `SpscQueue`/atomics discipline preserved verbatim.
4. **GUI never touches audio.** C# pushes input events and polls flat byte-array snapshots (LED RGB, OLED framebuffer, status) at ~30 Hz.

## Architecture

```
Pico2Seq.App (WinUI 3, C#)                      p2s_desktop.dll (CMake target, C++17)
├─ Virtual panel: 32-pad grid + LED glow,       ├─ engine: pico2seq-core + voice + rpdsp
│  128×64 OLED canvas, 4 faders, V1–V4, 8-button│ ├─ DesktopApp    — 1 kHz control thread (Application::begin/update port)
│  panel, mode-strap toggle, encoder dial,      │ ├─ HostClock     — uClock shim driven by audio sample counter
│  lidar hand-slider, play/stop                 │ │                  (480 PPQN, tempo 45–200, shuffle templates)
├─ P/Invoke flat C ABI:                         │ ├─ DesktopAudio  — miniaudio/WASAPI shared 48 kHz float stereo;
│  p2s_init/p2s_shutdown                        │ │                  callback runs VoiceManager::processBlock in
│  p2s_push_pad/fader/button/encoder/lidar      │ │                  256-frame chunks via Pcm16 (bit-parity)
│  p2s_poll_leds/oled/status                    │ ├─ DesktopStorage— std::fstream + atomic rename (replaces LittleFS)
└─ ~30 Hz DispatcherQueue timer                 │ ├─ InputRouter   — GUI events → ButtonHandlers/ControlSurfaceLogic/
                                                │ │                  AlchemyProto frames/Matrix/EncoderManager
                                                │ ├─ DisplayBridge — LED RGB array + 1bpp OLED framebuffer snapshots
                                                │ ├─ api.cpp       — C ABI
                                                │ └─ shims/        — host uClock driver, Adafruit_GFX-compatible
                                                │                    canvas (+glcdfont), FastLED CRGB, fake MPR121,
                                                │                    fake TMAG5273, Wire no-op, Serial→log sink
```

## Hardware → GUI mapping (parity checklist)

| Firmware hardware | Desktop replica |
|---|---|
| MPR121 32-pad matrix (`src\matrix\`) | 4×8 clickable pad grid: tap=toggle gate, hold=Step Edit, Shift+tap=clear, held-param+pad=track length, Settings=preset select (real Matrix logic vs fake MPR121 touch-bitmask driver) |
| WS2812 8×4 LED matrix + 10 themes | Pad cells glow with LEDMatrixFeedback colors (CRGB shim; poll snapshot) |
| SH1106 OLED 128×64, full view priority stack | WriteableBitmap 128×64 (nearest-neighbor upscale) fed by GFX-shim framebuffer — real `oled.cpp` logic runs unmodified |
| Alchemy tile: 4 faders + V1–V4 + 8-button panel + GP7 mode strap | Sliders / ToggleButtons / buttons; GUI synthesizes AlchemyProto frames → existing bridge/ControlSurfaceLogic decode |
| TMAG5273 velocity encoder (7 targets) | Rotary dial: mouse wheel + drag, velocity scaling from `SensorConstants` (real EncoderManager vs fake TMAG5273 angle source) |
| VL53L1X lidar 55–700 mm | Vertical mm ruler slider with no-hand zone → same `PerformanceInput::observeDistance` calibration + live recording semantics |
| LittleFS `/session.p2s` | `%APPDATA%\Pico2Seq\session.p2s`, identical ProjectSnapshotV1 bytes (interchangeable with hardware) |
| Watchdog + retained RAM | Dropped; autosave-on-stop + crash-recovery mirror file (reuses `RetainedSessionLogic`) |
| USB serial console / DIAG lines | Optional in-app log pane |

## Repo layout

```
desktop/
  native/            CMakeLists.txt → p2s_desktop SHARED lib + p2s_desktop_tests (Catch2)
    src/ DesktopApp / HostClock / DesktopAudio / DesktopStorage / InputRouter / DisplayBridge / api
    shims/  tests/
  app/               WinUI 3 C# solution (winapp scaffold)
```
Root `CMakeLists.txt`: `add_subdirectory(desktop/native)` behind `option(PICO2SEQ_BUILD_DESKTOP "OFF")`.

## Phases

**Phase 0 — Headless engine DLL (sound + clock).**
1. Scaffold CMake + shim skeleton (extend `tests/stubs` approach). Verify DLL links engine sources.
2. DesktopAudio (miniaudio via FetchContent): WASAPI 48 kHz; callback = 256-frame `processBlock` chunks → `toPcm16` → float out. Headless harness plays a preset chord.
3. HostClock: sample-counter 480 PPQN generator w/ tempo, start/stop, shuffle templates; `ClockService.cpp` compiles **unchanged** against the shim. Golden tick tests.
4. DesktopApp 1 kHz control thread: port `Application::begin/update` ordering (minus watchdog/LittleFS). Headless 10 s soak, ASan/TSan clean.

**Phase 1 — WinUI shell + core surface.**
5. winapp scaffold, P/Invoke layer, 30 Hz polling, layout (winui-design skill). Play/stop transports audio.
6. Pad grid + LED mirror + playhead.
7. OLED canvas: inventory `oled.cpp` GFX calls, shim + glcdfont; all views render (status screen first).

**Phase 2 — Full control-surface parity.**
8. Faders (Param/Utility), V1–V4 + Shift chords, 8-button panel, mode strap — via real ControlSurfaceLogic + AlchemyProto frames; check off `docs/manual.md` / `docs/ButtonHandlers.md` behaviors.
9. Encoder dial (velocity scaling, 7 targets, fine-edit) + Step Edit round-trip.
10. Lidar slider: armed-param live recording, octave zones per `SensorConstants`.
11. Settings mode: 29-preset browser + Sound Buffet, voice toggles.
12. Randomize/clear variants, scale/swing/theme cycles, gate-length gauge, long-press promotions.

**Phase 3 — Persistence & recovery.**
13. DesktopStorage: same snapshot bytes, atomic write, autosave-on-stop-if-CRC-changed, Save/Load; golden round-trip; hardware interchange test.
14. Crash-recovery mirror file via `RetainedSessionLogic`; kill-and-resume test.

**Phase 4 — Tests, packaging, docs.**
15. Native Catch2 suites (HostClock, DesktopStorage, InputRouter, DisplayBridge); full suite green.
16. WinUI UI batch tests (winui-ui-testing skill).
17. MSIX x64 packaging (winui-packaging skill).
18. This plan file + `docs/desktop-app.md` + README/CLAUDE.md updates.

## Risks & mitigations

- **uClock shuffle/tempo parity** — golden tests; compare vs hardware `[DIAG C0]` output if needed.
- **GFX/FastLED coupling** — inventory actual calls first; shim only what's used.
- **`ButtonHandlers.cpp` / `UIEventHandler.cpp` / `AlchemyControlBridge.cpp` never host-compiled** — expect small fixes; prefer shims; flag firmware edits.
- **WASAPI shared latency (~10 ms)** — acceptable; 256-frame internal cadence kept.
- **Concurrent editing** — stay inside `desktop/`, re-check `git status` before commits.

**Out of scope:** USB MIDI, hardware watchdog forensics, ASIO/exclusive mode, VST/plugin build, firmware behavior changes.
