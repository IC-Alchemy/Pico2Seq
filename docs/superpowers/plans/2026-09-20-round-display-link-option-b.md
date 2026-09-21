# Plan (Option B — verified): PY32 owns the round panel, Pico feeds it over I2C

Status: draft of 2026-09-20 (formerly `Z:\round-display-optionB-plan.md`) verified against
three evidence sources on 2026-09-20:

1. **Pico2Seq** working tree, branch `Display` @ b924c97 + uncommitted edits (all file:line refs below).
2. **Tile satellite work** on branch `claude/py32-state-packet-format-0e9lrh` (worktree
   `.claude/worktrees/preset-sweet-spot-modulation-006582`): `tiles/SliderModule/SliderModule.ino`,
   `tiles/ButtonModule8/ButtonModule8.ino`, `src/AlchemyUI/src/{AlchemyProto.h,SatelliteLink.h}`,
   `tests/py32_stubs/`. This branch is **unmerged** — nothing in it is on `DeCluttered` or `Display`.
3. **PY32F030 Datasheet V2.5 + Reference Manual V1.7** (download.py32.org).

⚠️ **`hypnosisPY32` could not be found** on this machine (full `Z:\` and profile search) or on
GitHub (IC-Alchemy). Everything in §5 that describes its internals (`gc9a01.c`, `engine.c`,
`board.c`, `board_millis()`, ping-pong `rowbuf`, 3952 B image / 2284 B RAM, `tools/verify_math.py`,
`make verify`) is a **[PY32-UNVERIFIED]** carryover from the session that drafted the plan and must
be re-checked when the repo is available. Everything else in this document is verified.

## 0. Corrections to the draft (why this version differs)

| Draft said | Verified reality | Fix in this plan |
|---|---|---|
| `kOledIntervalMs = 40` in ControlIO.cpp; caller `refreshOled()` | `kDisplayIntervalMs = 40` (`src/app/ControlIO.cpp:14`); caller `ControlIO::refreshDisplays(uint32_t)` (`ControlIO.cpp:256`) which also drives step LEDs + `ledMatrix.show()` in the same 40 ms slice | §4 uses real names |
| OLED pushes ~1 KB full frame per refresh | `commitFrame()` dirty-tracks per 128-byte page against `frameShadow_`; static screen = **zero** bus bytes (`src/OLED/oled.cpp:150-181`) | §2 bandwidth math redone honestly |
| "per-packet CRC8 … same convention as AlchemyProto.h" | AlchemyProto has **no CRC** — checksum is a truncating-uint8 **SUM** of STATUS+DATA (`AlchemyProto.h:163-180`); the only CRC in the repo is CRC-32 in `SnapshotFormat.h` (unrelated) | §3 uses SUM, not CRC8 |
| PY32 slave "NACKs a write only if both slots are full; Pico treats NACK as retry" | Tile slaves **never NACK**: over-reads pad 0x00, writes to unknown registers are silently drained (`SliderModule.ino:294-301,536`); hub policy is drop-and-wait, next scheduled poll recovers (`AlchemyTiles.h:36-38`); triple frame buffers make "full" unreachable (`SliderModule.ino:241-248`) | §3/§5 use the tile drop-and-wait pattern |
| theme_block carries `voiceHues[4]`, `playheadAccent`, `backgroundBase`, `textAccent` from `ALL_THEMES` | `LEDThemeColors` (`src/LEDMatrix/LEDMatrixFeedback.h:55-106`) has `gateOn[4]`, `playheadAccent`, 20+ LED-specific fields — **no `backgroundBase`, no `textAccent`**; colors are FastLED `CRGB` (RGB888), not RGB565; the OLED is monochrome and uses **no theme data today** (themes feed the LED matrix only; index = `uiState.currentThemeIndex`, persisted in `Session.cpp:52,136`) | §4 task adds the two fields to `LEDThemeColors` + all 10 `ALL_THEMES` entries; `gateOn[i]` serves as voice hue |
| `OLEDDisplay` interface = `begin/update/onVoiceSwitched/clear` | Also `update(uiState, seqView)` 2-arg, `isInitialized()`, `setVoiceManager()`, `onVoiceParameterChanged()`, `onVoiceSwitched(uiState, vm)` overload (`src/OLED/oled.h:55-111`); live object is `controls.display` inside `ControlHardware` (`ControlIO.cpp:23-32`); `extern OLEDDisplay oledDisplay` (`oled.h:204`) is dead | §4 swaps at `ControlHardware`, deletes the dead extern at cutover |
| Registers incl. `SUM`, `STATS` "same as AlchemyProto" | No `STATS` register exists on the wire — stats are hub-side counters (`SatelliteLink.h:190-194`). Tiles **hand-mirror** the header (`SliderModule.ino:93-97`), nothing is shared verbatim across repos | §3 defines a fresh register map in `display_link.h` that borrows conventions; STATS marked as a new extension; mirroring discipline documented |
| Notice kinds `RANDOMIZED/CLEARED/SAVED/LOADED/LOAD ERR` | `OledNoticeKind` has 8 kinds: + `VoiceCleared`, `AllCleared`, `DelayMix`, `DelayTime` (+ `oledNoticeValue` for delay) (`src/ui/UIState.h:36-41`) | §6 payload covers all 8 |
| "send to the PY32's I2C pins" (unnamed) | PY32F030 has **one** I2C; on the F2x 20-pin die pinout (the only one exposing PA4–PA7 **and** PB6/PB7) the pair is **PB6=SCL / PB7=SDA, AF6** (TSSOP20 pins 13/14). Backup: PA2/PA3 AF12. PB2 has no I2C function. | §2 wiring fixed to PB6/PB7 |
| (implicit) PY32 clock fine as-is | Tile sketches run HSI 4 MHz (`tiles/.vscode/arduino.json`); I2C Fast-mode slave needs PCLK ≥ 4 MHz (exactly met at 4 MHz); SPI1 SCK ≤ f_PCLK/2, so a 240×240×16-bpp fullscreen at 4 MHz ≈ 2 fps, at 48 MHz ≈ 26 fps ceiling | §2/§5 recommend 48 MHz SYSCLK [PY32-UNVERIFIED what board.c sets today] |

Verified and kept as-is: `Wire` = I2C0 GP4/GP5 @ 400 kHz (`ControlIO.cpp:14,66-69`); `Wire.setTimeout(25, true)` (`ControlIO.cpp:70`); bus residents OLED 0x3C / VL53L1X 0x29 (`SensorConstants.h:23`) / TMAG5273 0x35 (`TMAG5273.h:46`) / MPR121 0x5A (`ControlIO.cpp:18`); **0x3E unused**; tiles live on `Wire1` 0x08–0x0D (different bus, no conflict); page priority order (§6); `VoiceEdit::name/format` (`src/voice/VoiceEditParameters.h:117-155`); `MusicalValues::format` (`src/voice/MusicalValues.h:84-85`); 29 presets (`VoicePresets.cpp:92`); SEQ = 4-bit, wrap-at-15, equality-only (`AlchemyProto.h:10-13,108-112`).

## 1. Architecture (unchanged from draft, ground rules preserved)

- **Panel wiring untouched**: CS/SCK/DC/MOSI/RST = PA4/PA5/PA6/PA7/PF0 on SPI1 + DMA row push [PY32-UNVERIFIED file refs].
- **Pico decides WHAT, PY32 decides HOW.** Pico keeps the page priority hierarchy (§6) and
  serializes the winning page + fields; PY32 owns fonts, round layout, theme application,
  animation, GC9A01 driving.
- **Pico never sends pixels.** 240×240×16-bit = 115 KB > 4 KB PY32 SRAM (PY32F030x6 = 32 KB
  flash / 4 KB SRAM, confirmed). Only fixed-size state packets (≤ 96 B) cross the wire.

## 2. Transport

- **Bus**: existing main `Wire` (I2C0, GP4/GP5, 400 kHz). Display slave at **0x3E** (verified free;
  OLED stays 0x3C during migration for side-by-side, reclaim later).
- **Wiring**: SDA/SCL/GND from the main bus to **PY32 PB7 (SDA) / PB6 (SCL), AF6**, with external
  pull-ups to 3V3 (same as tiles). These are the only free complete I2C pair on the F2x 20-pin
  pinout that also exposes PA4–PA7. Nothing else on the Pico harness changes.
- **Clock [PY32-UNVERIFIED current setting]**: recommend 48 MHz SYSCLK (HSI+PLL). Constraints:
  I2C Fast-mode slave needs PCLK ≥ 4 MHz; SPI1 SCK ≤ f_PCLK/2 gives a fullscreen 16-bpp ceiling of
  ~26 fps at 48 MHz (~2 fps at the tiles' 4 MHz). PF0 in use as panel RST already implies
  HSI-based clocking (PF0 = OSC_IN), so PLL-from-HSI is available.
- **Cadence**: send at the existing display tick (`kDisplayIntervalMs = 40`, ≤ 25 Hz),
  **only when the serialized page bytes differ from the last-sent shadow**, plus a 2 Hz heartbeat
  (identical SEQ, re-sendable frame — idempotent on the PY32).
- **Bandwidth (honest math)**: worst case today (full page switch) the OLED pushes up to
  8×128 B dirty pages ≈ 1 KB+; a display frame is ≤ 96 B. Static screens: OLED = 0 B, display
  heartbeat = 192 B/s. So busy screens get ~10× cheaper, idle screens cost 192 B/s — net win on a
  bus that also carries VL53L1X/TMAG5273/MPR121 polling.
- **Robustness (tile-proven patterns, copied)**:
  - Hub side: `Wire.setTimeout(25, true)` already guards the bus; on any send failure, drop and
    wait — the next 40 ms tick re-sends because the shadow is still dirty (never retry in-line;
    `AlchemyTiles.h:36-38` discipline).
  - Wire format: truncating-uint8 **SUM** + 4-bit SEQ, equality-only comparison, wrap at 15
    (`AlchemyProto.h` conventions verbatim).
  - Slave side: never NACK; validate in main loop; ~50 ms bus watchdog + peripheral rebuild +
    `i2cResetTransactionState()` pattern from `SliderModule.ino:362-372,899-920`; IWDG stays.
  - Link loss on PY32: no valid frame for 2 s → hold last frame, then idle animation (dimmed
    backdrop) — never black-screen.

## 3. Protocol — `display_link.h`

New header, pure C (no Arduino/Pico/PY32 includes), canonical in **Pico2Seq at
`src/RoundDisplay/display_link.h`**, manually mirrored byte-identical into hypnosisPY32 with the
same "keep this block byte-identical" discipline the tiles use (`SliderModule.ino:93-97`) — repos
cannot share verbatim; that's how AlchemyProto actually works today.

Borrowed conventions (values from `AlchemyProto.h`): `WHOAMI = 0x5A` at reg 0x00, `TYPE_ID` 0x01,
`PROTO_VER` 0x02, `FW_VER` 0x03 (2 B), `STATUS` 0x20 with `HEARTBEAT=0x01`, `LOCAL_FAULT=0x02`,
`NOT_READY=0x04`, SEQ in bits 4–7. New for this link:

- `kTypeDisplay = 0x03` (next free after slider 0x01 / button 0x02).
- **Read path (identity/liveness/debug, cold)**: register-pointer read like tiles — pointer write
  terminated by STOP, then block read (`AlchemyTiles.cpp:155-163` shape).
- **STATS registers 0x24–0x27** (frames accepted, SUM rejects, frames rendered, last applied SEQ) —
  *new extension*, not a copy; stats never existed on the wire before.
- **Write path (hot)**: hub writes pointer to `kRegFrame = 0x21`, then streams the PAGE_FRAME body
  with auto-increment, STOP-terminated. Body, all little-endian, fixed size per page:

```
PAGE_FRAME := protoVer(1) seq(1) pageId(1) themeIdx(1)
              themeBlock(14)            // 7× RGB565: voiceHue[0..3] (= gateOn[i]), playheadAccent, backgroundBase, textAccent
              body(pageId-specific, ≤ 64 B)
              sum(1)                    // truncating uint8 SUM of all preceding bytes
```

- **SEQ semantics**: Pico increments SEQ only when page content changes (not per heartbeat
  re-send). PY32 applies a frame only if SUM ok **and** `seqChanged(frame.seq, applied.seq)`;
  identical SEQ re-send = already-applied, no re-render. PY32 echoes last-applied SEQ in STATUS so
  the Pico can confirm receipt in debug polls (liveness proof = heartbeat toggling or SEQ moving —
  the "sweeping, not answering" rule, `SatelliteLink.h:110-128`).

## 4. Pico2Seq work (branch `Display`)

All names verified against the working tree. OLED code stays untouched until Phase 4 (there is no
existing compile-flag mechanism — firmware builds via `scripts/build_pico2seq.ps1`/arduino-cli —
so migration runs both displays live and deletes OLED at cutover).

1. **`src/RoundDisplay/display_link.h`** — protocol constants, PAGE_FRAME layout, per-page body
   structs, `frameSum`/`sumOk` (copy semantics from `AlchemyProto.h:163-180`), `seqChanged`
   (equality-only). No dependencies.
2. **Theme additions** in `src/LEDMatrix/LEDMatrixFeedback.h/.cpp`: add `CRGB backgroundBase` and
   `CRGB textAccent` to `LEDThemeColors`; add values to all 10 `ALL_THEMES[]` entries
   (`LEDMatrixFeedback.cpp:97`). The `static_assert`s at 427-429/443-444 keep the table honest.
   LED matrix ignores the new fields; `uiState.currentThemeIndex` already selects + persists.
3. **`src/RoundDisplay/RoundDisplayLink.h/.cpp`** — class exposing the same surface `ControlIO`
   needs: `begin()`, `update(const UIState&, const SequencerView&, VoiceManager*)`,
   `onVoiceSwitched(const UIState&, VoiceManager*)`, `onVoiceParameterChanged()`, `clear()`,
   `isInitialized()`. It re-implements only the **page-selection chain** (the 9 gates in §6,
   mirroring `src/OLED/oled.cpp:243-506`) and serializes the winner; value formatting is reused
   untouched (`MusicalValues::format`, `VoiceEdit::name/format`, `VoicePresets::getPresetName`).
   Sends only when serialized bytes differ from `lastSent_` shadow; 2 Hz heartbeat re-sends.
4. **`ControlIO` wiring**: add `RoundDisplayLink roundDisplay;` to `ControlHardware`
   (`ControlIO.cpp:23-32`); `beginDisplay()` starts both; `refreshDisplays()` (`ControlIO.cpp:256`)
   drives both from the same 40 ms slice. Delete OLED + dead `extern OLEDDisplay oledDisplay`
   (`oled.h:204`) + `OLEDConstants` (`LEDConstants.h:72-93`) at Phase 4.
5. **Docs at cutover**: `docs/oled.md` → `docs/round-display-link.md` (note: oled.md already has
   drift — stale `update()` signature at :235, shuffle claim at :174, "delay notices removed" at
   :103 vs live `DelayMix/DelayTime` — don't port those errors).

## 5. PY32 work (hypnosisPY32) — [PY32-UNVERIFIED structure]

Ground rule: `gc9a01.c` init table, `board.c` clocks, DMA row path stay as-is except the frame
source. Copy the slave skeleton from `tiles/SliderModule/SliderModule.ino` (register-level ISR;
the PY32Duino `Wire` slave path is known-broken on this exact core — `tiles/README.md:65-72`):

1. **I2C slave ISR** modeled on `I2C1_IRQHandler()` (`SliderModule.ino:558-627`): direction latch
   at ADDR, one DR write per TXE|BTF slot, bounded RX drain, pointer-parking, never NACK. RX
   lands PAGE_FRAME bytes into a double-buffered slot pair; ISR only copies + counts. Main loop
   validates SUM/SEQ and swaps the shadow. Include `i2cResetTransactionState()`, bus watchdog,
   IWDG blocks from the tile.
2. **`display_state`**: shadow of last good frame + `link_alive` from `board_millis()`
   [PY32-UNVERIFIED symbol]; >2 s stale → idle page (dimmed `engine_frame()` backdrop — the
   hypnosis animation survives as screensaver).
3. **Renderer**: `pages.c`/`text.c`/`shapes.c` on the existing row-scan DMA pattern — ring/arc,
   bars, 16 step dots on a chord, 1-bit text. **Fonts in flash, not RAM** (glyph scanlines fetched
   per row; ~28 KB flash free claimed, 4 KB RAM confirmed by datasheet; I2C buffers ~2×96 B +
   shadow ~96 B fit). Budget note: at 48 MHz a fullscreen 16-bpp push is ~38 ms — fine for 25 fps
   of mostly-static pages, but the renderer should push dirty rows only where the layout allows.
4. **Main loop**: poll I2C shadow → select page renderer → row render via DMA ping-pong → update
   STATUS/STATS.

## 6. Page selection + payloads (Pico side)

Chain verified at `src/OLED/oled.cpp:243-506`, highest priority first; `pageId` values in
`display_link.h`:

| # | pageId | Gate (exact) | Body fields (source identifiers) |
|---|---|---|---|
| 1 | `VOICE_EDITOR` | `uiState.voiceEditor.active` | voice idx, `voiceEditor.changed[voice]` (`*`), param name/value via `VoiceEdit::name/format`, lane via `VoiceEdit::laneName` |
| 2 | `MODE_BANNER` | `millis() < uiState.alchemyModeBannerUntil` | PARAM/UTIL word, remaining theme-mode color |
| 3 | `NOTICE` | `millis() < uiState.oledNoticeUntil && oledNoticeKind != None` | kind (all 8: Randomized/Saved/Loaded/LoadError/VoiceCleared/AllCleared/DelayMix/DelayTime), `oledNoticeVoice`, `oledNoticeValue`; 800 ms window (`UIConstants.h:42`) |
| 3b | `HELD_PARAM` | `getHeldParameterParamId(uiState) != ParamId::Count` | param id, voice, `selectedStepForEdit`, formatted value (`MusicalValues::format`), `handPresent`, `distanceSensor.getRawDistanceMm()`, BASE vs LIVE/STEP (`encoderBaseViewUntil`, `MusicalValues::baseStep`) |
| 4 | `SETTINGS_VOICE_TOGGLES` / `SETTINGS_PRESETS` | `uiState.settingsMode` + `isVoiceParameterSettings()` else `isPresetSelection()` | toggles: `voiceParameterNoticeName/Value/Voice`; presets: `voicePresetIndices[4]`, `VoicePresets::getPresetName/getPresetCount` (29) |
| 5 | `GATE_LENGTH` | `uiState.gateSeqLengthMode` | voice, `sequence.getParameterStepCount(ParamId::Gate)` (1–64, `SequencerDefs.h:20`) |
| 6 | `STEP_ENV` | step selected && (`editing == Count` ‖ `millis() < envViewUntil`) | step, `kLanes{A,D,S,R}` values via `getStepParameterValue`, `followsPatch()` per lane, `envFaderLane` marker |
| — | `PARAM_EDIT` | `editing != Count && (held ‖ selected)` | same as HELD_PARAM |
| 7 | `STATUS` | default | `voicePresetIndices[4]`+names+`changed[]`, `uClock.getTempo()`, `getCurrentStep()`, `currentScale`/`scaleNames`, `currentShufflePatternIndex` + `getShuffleTemplateName()` (helper exists unused at `ShuffleTemplates.h:44` — new on-screen), `VoiceEditor::encoderTarget()` + formatted value, per-step gate bits (`getStepParameterValue(ParamId::Gate, i)`) |

Layout language on the round panel (all pages): outer ring = theme accent + progress/playhead
arc; top arc = `Vn` + preset name; center = primary value (large font); mid band = secondary
fields; bottom chord = 16 step dots + playhead; footer = context hint.

## 7. Delivery phases

- **Phase 0 — protocol + presence**: `display_link.h` in Pico2Seq (+ mirrored copy placeholder);
  PY32 slave skeleton + identity regs. Exit: `i2c scan` shows 0x3E; WHOAMI/STATUS readable.
- **Phase 1 — one page end-to-end**: Pico sends `STATUS`, PY32 renders text + ring + step dots.
  Exit: live photo; STATS zero rejects.
- **Phase 2 — all pages**: remaining renderers + theme block; OLED and round run side-by-side.
  Exit: every OLED screen has a round equivalent.
- **Phase 3 — hardening**: link-loss pull-test (idle animation, resume on reconnect), heartbeat +
  sensor load soak, 10-theme legibility sweep. Exit: overnight, zero bus stalls.
- **Phase 4 — cutover**: delete `src/OLED/`, `OLEDConstants`, dead extern; rewrite docs.

## 8. Testing

- **Host (Pico2Seq suite, per CLAUDE.md)**: `tests/unit/test_display_link.cpp` (SUM accept/reject,
  SEQ wrap-at-15 equality, per-page encode/decode round-trip — style of
  `test_alchemy_proto.cpp:136-195`) and `tests/unit/test_round_display_link.cpp` (page-selection
  chain + dirty-shadow behavior with stubbed `Wire` from `tests/stubs`). Register in
  `tests/CMakeLists.txt` as new `add_executable` + `catch_discover_tests` (and while there: the
  source list currently lists `test_master_delay.cpp` twice — harmless, leave or fix).
- **PY32**: mirror the `tests/py32_stubs/` technique in-repo — a host TU that includes the slave
  source and flag-drives `I2C1_IRQHandler()` (pattern: `TileHarness.h` `tile::Master`), asserting
  RX-slot, SUM/SEQ acceptance, drop-and-wait, pointer parking. Layout preview to PNG via
  `tools/verify_math.py` extension [PY32-UNVERIFIED]. If instead the display tile is built under
  `tiles/` in Pico2Seq, the existing harness works as-is (agent-verified: the RX path is exactly
  what `tile::Master` exercises).
- **Hardware**: I2C scan at 0x3E; logic-analyzer capture of a PAGE_FRAME burst; fps + STATS read;
  SDA-disconnect pull-test; all-10-theme photo sweep.

## 9. Open questions (need user input)

1. **Where is `hypnosisPY32`?** Not on this machine or GitHub — every [PY32-UNVERIFIED] tag above
   waits on it. (The drafting session's worktree `f12b2e89` is also gone.)
2. Is the panel MCU confirmed a PY32F030**F2x** 20-pin part? PB6/PB7 only exist on F2x (F3x lacks
   them; F4x shares PB6 with SWCLK). Tiles use PY32F030F28U6TR (an F2x part) — same assumption.
3. Should the `claude/py32-state-packet-format-0e9lrh` tile work land on `DeCluttered` first? This
   plan copies its slave pattern and proto conventions, and the py32 host-test harness only exists
   on that branch.
4. What does hypnosisPY32's `board.c` actually clock today? The 48 MHz recommendation (§2) may
   already be true, or a change guarded by re-validating the GC9A01 init sequence timing.
