# Preset Studio — your own voice presets, designed on the computer

Preset Studio is a Windows program for designing voice presets with sliders, a plain-language
explanation of every value, and live listening, then sending them to the Pico over the same USB cable
you program it with. Sent presets live in flash (they survive power-off) and appear as **extra pages on
the box's preset browser**, each preset at the pad you chose and lit in the colour you chose.

- [1. For players](#1-for-players)
- [2. What the box does with them](#2-what-the-box-does-with-them)
- [3. Limits and what is not editable](#3-limits-and-what-is-not-editable)
- [4. Troubleshooting](#4-troubleshooting)
- [5. For developers](#5-for-developers)
  - [5.1 How the pieces fit](#51-how-the-pieces-fit)
  - [5.2 Source map](#52-source-map)
  - [5.3 The user preset record](#53-the-user-preset-record)
  - [5.4 The bank file on flash](#54-the-bank-file-on-flash)
  - [5.5 The wire protocol](#55-the-wire-protocol)
  - [5.6 Library and preset files on the computer](#56-library-and-preset-files-on-the-computer)
  - [5.7 One source of truth: generated resources](#57-one-source-of-truth-generated-resources)
  - [5.8 Changing the layout](#58-changing-the-layout)
  - [5.9 Safety design](#59-safety-design)
  - [5.10 Tests](#510-tests)
  - [5.11 Building and publishing the Windows program](#511-building-and-publishing-the-windows-program)

---

## 1. For players

### 1.1 What you need

- A Windows 10 or 11 PC with the [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)
  (or publish a self-contained build, see [§5.11](#511-building-and-publishing-the-windows-program)).
- The Pico2Seq firmware built from this repository (it contains the preset link). Flash it the usual way.
- A USB **data** cable. No driver is needed: the Pico appears as a standard COM port.
- Close the Arduino serial monitor or any terminal first. Only one program can hold the port.

### 1.2 Make and send a preset

1. **Connect.** Start Preset Studio, leave the port on *Auto-detect* and click **Connect**. The dot beside the
   port turns green and the status bar says how many user presets the Pico holds and how much storage is free.
2. **Start from a factory sound.** Click the blue **New preset from factory sound** button and pick the preset
   closest to what you want. Every preset is built on a factory preset: it supplies the sound engine and what
   the sequencer's lanes do on this sound.
3. **Change it.** The tabs in the middle hold every value of the sound: *Sound*, *Oscillators*, *Engine*,
   *Envelope*, *Filter*, *Overdrive* and *Performance*. Point at any value and the panel on the right explains what it
   does, what you will hear, its range, and its value in the base preset. A value you changed gets an
   **amber bar** and a small **↺** button that puts it back.
   - Values that do not apply to the sound as it is now (say, string controls on an oscillator voice) are dimmed;
     point at one to see why.
   - Type a value if you prefer: `250ms`, `1.5 s`, `37%`, `440hz`, `1.2k`, `+7`, `off`, or a choice's name.
4. **Hear it.** Choose a voice (1–4) next to **Play on voice** and switch **Live** on. The selected preset now
   plays on that voice of the Pico while you move the sliders, and when you pick a different preset in the list.
   Start the sequencer on the box to hear it in your pattern. **Play now** sends it once without Live.
5. **Name it, colour it, place it.** Give it a short name (up to 15 characters), pick the pad colour (palette or
   RGB sliders), and choose its *Page* and *Pad* — or use the grid on the right: click an empty pad to move
   the selected preset there, or drag presets onto pads to move and swap them. Pad 31 (grey) is the page key on
   the box and cannot hold a preset.
6. **Save on your computer.** **File → Save** writes your whole library to a readable `.p2lib` file.
   **File → Export** writes one preset to a `.p2preset` file you can share or import elsewhere.
7. **Send to the Pico.** Click **Send to Pico**. Preset Studio first shows exactly what will change (new,
   changed and removed presets, and what changed inside each) and whether it fits in storage. Confirm, and the
   library is written to flash. The Pico pauses its sequencer for about a second while it saves, then carries
   on. Preset Studio then reads everything back and checks it against what it sent.

**Load from Pico** does the reverse: it reads the Pico's presets into the editor, so a library is never trapped
on one computer. **Pico → Grab the sound playing on voice N** brings in whatever a voice is playing right now,
including tweaks made with the box's own knobs, as a new preset.

### 1.3 Good habits

- Keep your library file. The Pico holds a copy, but the file is where your notes, and a backup, live.
- The Pico's pages mirror the library: sending replaces the Pico's user presets with the library's. The preview
  shows removals before they happen.
- Use colours as a code (basses blue, leads orange, pads green). The LEDs are bright and saturated: pale colours
  look white, very dark ones look off.

---

## 2. What the box does with them

- **Preset browser pages.** Stop the transport (or long-press Play) to open the preset browser. Page 1 is the
  factory bank, as always. Once any user preset exists, **pad 31** lights dim white and flips to the next page
  that has presets; the pages wrap round. With no user presets, nothing changes: pad 31 stays unassigned.
- **LEDs.** On a user page each preset's pad shows its own colour: a quarter strength at rest, breathing at full
  strength for the preset the selected voice is playing. Empty pads stay dark.
- **Choosing a voice** works as before (V1–V4); tapping a user preset's pad applies it to that voice, staged
  exactly like a factory preset, so you can audition live while the transport runs.
- **OLED.** The browser shows the page (`P2/3 USER 6 presets`) and the preset's name; a voice's header and the
  sound-buffet list show the user preset's name instead of its factory base.
- **Saving the song.** A voice keeps the sound you gave it in the song file (all its values), plus a two-byte tag
  recording which user pad it came from, so after a power cycle the browser still highlights the right pad and
  shows the right name. If you later replace the bank, the voice keeps sounding the same and keeps its name; it
  just stops owning a pad.
- **While you transfer.** Starting a bank transfer stops the clock (flash writes stall the cores for
  milliseconds, the same reason the song save stops it) and restarts it afterwards; the OLED shows *PC LINK
  receiving presets*, then *PRESETS n saved*. The song autosave waits until the transfer is over.

---

## 3. Limits and what is not editable

| Limit | Value | Why |
|---|---|---|
| Presets | 62 (2 user pages × 31 pads) | Pad 31 is the page key. The whole bank file is 15,888 bytes at most (4 flash blocks), sized to share the 64 KB filesystem with the song file |
| Name | 15 plain ASCII characters | Fits the record; the OLED shows ≤10 characters large |
| Pad | 0–30 on pages 2 and 3 | Page 1 is the factory bank and cannot be changed |
| Recipe engine | Only on a recipe base | Recipes are code in flash; a preset can choose one only by being built on a preset that has it |
| Sequencer lanes | As the base preset defines them | A lane layout is a flash descriptor, not a number; changing the engine away from the base's engine uses the engine's standard layout |

You can change **every numeric and switch value of the sound** (69 of them: bases, oscillators, envelope, filter,
high-pass, overdrive, string/hypersaw/noise/recipe controls, output). You cannot author new recipes, new lane
layouts, patterns, tempo, effects or tuning here — those are firmware or song state.

---

## 4. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| "No Pico2Seq found" | Wrong cable (charge-only), another program holds the port (close the Arduino serial monitor), or the firmware predates the preset link. Pick the COM port by hand if auto-detect finds none |
| "…stores presets in an older/newer layout…" | Preset Studio and the firmware were built from different versions of the patch layout. Update the older one |
| "The Pico refused 'X': Resonance … is outside what this sound allows" | A value outside the range for this preset's engine/base. The editor normally prevents it; it can happen with a hand-edited file. Fix the value and send again. The Pico kept its old presets |
| "The Pico does not have room" | Storage is full (the song file and the bank share 64 KB). Send fewer presets |
| The Pico does not answer mid-transfer | Pulled cable or crashed program. Whatever was half-sent is discarded after a few seconds; the previous presets are untouched |
| Presets are there but pad 31 does nothing | No user presets on the Pico yet, or you are not in the preset browser (stop the transport or long-press Play) |
| Live audition stops | The cable was disconnected, or the box refused a value (see the status bar). Reconnect and switch Live on again |

---

## 5. For developers

### 5.1 How the pieces fit

```
 Windows PC                                   Pico 2 (Core 0 only)
 ┌──────────────────────────────┐             ┌───────────────────────────────────────────────┐
 │ PresetStudio.App (WPF)       │             │ src/app/PresetLinkService   Serial ⇄ frames   │
 │   MainWindow.xaml            │             │   src/presetlink/PresetLinkProtocol  parser   │
 │ PresetStudio.Core            │   USB CDC   │   src/presetlink/PresetLinkSession   commands │
 │   Shell/MainViewModel        │ ══════════▶ │   src/presetlink/UserPresetStore     bank I/O │
 │   Editing/DeviceSession      │  A5 5A …    │ src/voice/UserPresetCodec   validate/canonical│
 │   Link/DeviceLink + parser   │ ◀══════════ │ src/voice/PatchFields       the field table   │
 │   Codec/RecordCodec          │             │ src/app/UserPresetStorage   LittleFS file     │
 │   Schema/PatchSchema ◀───────┼─ generated ─┤ src/ui/PresetBrowser        pads → presets    │
 └──────────────────────────────┘  from C++   └───────────────────────────────────────────────┘
```

Everything on the Pico runs on **Core 0, in the main loop**, in short slices; nothing touches the audio core
except through the existing voice publisher. Audio is unaffected by a transfer except that the clock is stopped
for the flash writes.

### 5.2 Source map

| Area | Files |
|---|---|
| Record and bank file (portable) | `src/pico2seq-core/persistence/UserPresetBank.h/.cpp`, incremental CRC in `SnapshotFormat.h` |
| Field table (offsets, types, ranges, rules) | `src/voice/PatchFields.h/.cpp`, `VoiceEdit::limits()` in `VoiceEditParameters.*` |
| Validation, canonical form, record ⇄ `VoiceConfig` | `src/voice/UserPresetCodec.h/.cpp` (built on `voicecodec::applyPatch`) |
| Wire protocol, store, command session (portable) | `src/presetlink/*` |
| Firmware glue | `src/app/UserPresetStorage.*`, `src/app/PresetLinkService.*`, `applyUserPreset()` in `src/app/VoiceSetup.*`, session tag in `src/app/Session.cpp` |
| Browser pages | `src/ui/PresetBrowser.h`, `UIState::presetPage/voiceUserSlot/voiceUserName`, `src/ui/UIEventHandler.cpp`, `src/LEDMatrix/LEDMatrixFeedback.cpp`, `src/OLED/oled.cpp` |
| Host tests | `tests/unit/test_user_presets.cpp`, `test_preset_link.cpp`, `test_preset_browser.cpp`, `test_preset_resources.cpp`; helpers in `tests/support/`; simulator `tests/tools/preset_link_sim.cpp` |
| Windows program | `tools/PresetStudio/` — `PresetStudio.Core` (everything but windows), `PresetStudio.App` (WPF), `PresetStudio.Core.Tests` |

### 5.3 The user preset record

256 bytes, little-endian, fixed; one per preset (`persistence::UserPresetRecord`).

| Offset | Size | Field |
|---|---|---|
| 0 | 16 | `name` — printable ASCII, NUL-terminated, every byte after the terminator zero |
| 16 | 1 | `page` — browser page 1..2 (page 0 is the factory bank) |
| 17 | 1 | `pad` — 0..30 (pad 31 is the page key) |
| 18 | 1 | `baseIndex` — factory preset supplying the flash-resident lane layout and recipe |
| 19 | 3 | `colorR, colorG, colorB` — LED colour |
| 22 | 1 | `flags` — zero |
| 23 | 1 | reserved — zero |
| 24 | 232 | `PatchSnapshot` — the same patch struct the song file stores (`ProjectSnapshot.h`) |

Inside the patch, `presetIndex` mirrors `baseIndex`, `flags` bit 0 (use-patch-bases) is always set, and
`paramSet` is **derived** from the engine and hard-sync waveforms by `usercodec::canonicalize()`; the editor
mirrors that rule and the firmware's version wins. A record is rebuilt into a playable `VoiceConfig` by
`voicecodec::applyPatch(baseIndex, patch)`, so a user preset loads through the very path a saved song does.

Which bytes of the patch are editable, and their keys, types and ranges, is the table in
`src/voice/PatchFields.cpp` (69 rows). Labels, units, groups and ranges are not copied: they come from the
on-device editor's catalog (`VoiceEdit::parameter()`), so the box and the editor describe a value identically.
The rows that differ from the catalog are explicit overrides — today only `hp.cutoff`, which stores 0 for "off".

### 5.4 The bank file on flash

`/presets.p2u` on LittleFS (written through `/presets.tmp` and renamed, like `/session.p2s`).

```
[12-byte header][count × 256-byte record][4-byte CRC-32 of header + records]
header: magic 'P2UB' (u32 0x42553250), version u16 = 1, recordSize u16 = 256, count u16, reserved u16 = 0
```

- The CRC **trails**, so the Pico can stream records to flash as they arrive without buffering the 15 KB bank in
  RAM (the heap has little headroom — see [audio-performance.md](audio-performance.md)).
- At boot `UserPresetStore::load()` verifies the whole file (a torn file shows nothing, never half a bank),
  re-validates every record (a firmware that tightens a limit drops the records it no longer accepts) and builds
  a 62-entry in-RAM directory (name, colour, base, file position). Applying a pad reads one record from flash,
  validates it again and publishes it.
- Footprint: about 16.5 KB of flash code and about 4.5 KB of static RAM for the whole feature.
- Flash budget (an estimate from LittleFS's 4 KB blocks, not yet measured on a board): the song file is 12.5 KB
  (4 blocks) plus its temp copy during a save, the bank at most 4 blocks plus its temp copy during an upload, and
  2 metadata blocks, so the worst case is about 14 of the 16 blocks of a 64 KB filesystem. `UserPresetFile::canWrite()` refuses an upload that cannot fit **before** the first byte is
  written. (Building with a larger filesystem — `flash=4194304_262144` — removes the constraint, but moves the
  filesystem and reformats it once, so it is not done silently.)

### 5.5 The wire protocol

Frames over the USB CDC serial port (`Serial`), 115200 baud nominal (CDC ignores the rate), DTR raised:

```
0xA5 0x5A | type u8 | seq u8 | length u16 | payload[length] | crc32 u32     (little-endian)
```

The CRC-32 (IEEE, the same one as the song file) covers `type..payload`. The sync bytes have the high bit set,
so they cannot occur in the firmware's ASCII log, which shares the port: the parser passes every byte that is not
part of a frame back as *console input*, and the host ignores or displays it. The maximum payload is 288 bytes.
Stop-and-wait: the host sends one request, waits for the reply with the same `seq` (1.5 s), and repeats only
requests that are safe to repeat (hello, reads, audition). A half-received frame is dropped after 250 ms of
silence.

**Requests** (`type`); a reply has `type | 0x80` and the same `seq`; an error reply has `type 0xFF`.

| Cmd | Name | Request payload | Reply payload |
|---|---|---|---|
| 0x01 | Hello | — | 28 bytes: `protocol u8, flags u8 (bit0 transport running, bit1 uploading), layoutVersion u16, recordSize u16, patchSize u16, factoryCount u8, userPages u8, padsPerPage u8, reserved u8, maxPresets u16, presetCount u16, freeBytes u32, bankBytes u32, tableHash u32` |
| 0x02 | BankBegin | `count u16` | `count u16, fileBytes u32` — stops the clock, opens `/presets.tmp` |
| 0x03 | BankPut | one 256-byte record | `received u16, slot u8` — canonicalised, validated, appended |
| 0x04 | BankCommit | — | `count u16, crc u32` — writes the trailer, renames over the live file, refreshes the browser |
| 0x05 | BankAbort | — | — (idempotent) |
| 0x06 | BankRead | `index u16` | the record at that file position |
| 0x07 | Audition | `voice u8` + record | — plays it on voice 0–3 through the encoder-edit publisher |
| 0x08 | FactoryRead | `index u8` | factory preset *index* as a record |
| 0x09 | VoiceRead | `voice u8` | the voice's current sound as a record |

**Errors** — payload `command u8, code u8, detail u8, aux u8`:

| Code | Meaning | detail / aux |
|---|---|---|
| 1 UnknownCommand | firmware lacks the command | |
| 2 BadPayload | wrong length | |
| 3 BadState | e.g. BankPut with no BankBegin | |
| 4 Busy | an upload is in progress | |
| 5 NoSpace | the bank will not fit | |
| 6 Storage | flash write/rename failed (upload abandoned, old bank kept) | |
| 7 InvalidRecord | validation failed | `detail` = `usercodec::Problem` (1 name, 2 place, 3 base, 4 reserved, 5 field, 6 recipe), `aux` = field-table row for 5 |
| 8 OutOfRange | index/voice does not exist | |
| 9 SlotTaken | two records claim one page/pad | |
| 10 CountMismatch | BankCommit before `count` records arrived | |

An upload that falls silent for 5 s is abandoned by the Pico (temp file deleted, clock restarted). A refused
record leaves the upload open, so the editor can resend a fixed one or abort.

**Compatibility.** The editor compares the Hello reply with the schema it was built from — protocol version,
layout version, record and patch size, preset geometry and the field-table hash — and refuses to send anything
on a mismatch, with a message saying which side to update.

**The `W` bench key.** `Application::update()` used to treat any received `W` as "freeze Core 0 to test crash
resume". All serial input now goes through the frame parser first, and a stray `W` is ignored while the editor
was heard in the last 3 s, so a damaged frame can never reboot the box.

### 5.6 Library and preset files on the computer

Readable JSON (`.p2lib`, `.p2preset`; see `tools/PresetStudio/PresetStudio.Core/Storage/LibraryFile.cs`).
Presets are stored by **value**, and the base by **name**, so a file survives a firmware update that adds
factory presets. Pages are written as the number the box shows (the first user page is `2`). Values the file
lacks come from the base preset; values from a newer layout are ignored; a newer `layoutVersion` is refused with
advice. Saves go through a temp file and a rename.

### 5.7 One source of truth: generated resources

`tools/PresetStudio/PresetStudio.Core/Resources/` holds four JSON files compiled into the program:

| File | Made by | Contents |
|---|---|---|
| `patch-schema.json` | `tests/unit/test_preset_resources.cpp` from `PatchFields` + the editor catalog | every value: key, offset, type, label, unit, range, choices, when it applies; record and browser geometry; per-engine range overrides |
| `factory-presets.json` | the same | each factory preset as editor values, with its lane names, macro spans, cutoff curve and range overrides |
| `golden-vectors.json` | the same | every factory preset as the exact 256 bytes the firmware stores, plus a scripted session (request and reply frames) |
| `field-help.json` | **by hand** | the explanation of every value and the tab layout |

The three generated files are **checked in**, and `[resources]` fails if they differ from what the firmware
tables produce, so they can never drift. The C# tests replay the golden vectors (byte-exact records, byte-exact
frames, the firmware's replies), which is how the independent C# codec is proved identical to the C++ one.

### 5.8 Changing the layout

Add a value, change a range, or reorder anything in the field table or `PatchSnapshot`:

1. Edit `src/voice/PatchFields.cpp` (and `PatchSnapshot` plus a song-format bump if the struct itself changes —
   see [persistence.md](persistence.md)).
2. Bump `patchfields::kLayoutVersion` in `PatchFields.h`.
3. `./build_test_ninja/tests/pico2seq_tests "[userpreset]"` fails on the pinned table hash; paste the new hash
   into `test_user_presets.cpp`.
4. Regenerate the editor resources and commit them:
   `PICO2SEQ_UPDATE_RESOURCES=1 ./build_test_ninja/tests/pico2seq_tests "[resources]"`.
5. Write the help text for the new value in `field-help.json` and place it on a tab (the C# tests insist).
6. `dotnet test tools/PresetStudio/PresetStudio.Core.Tests` — an old editor now refuses a new firmware, and the
   other way round, by design.

### 5.9 Safety design

- **Nothing unvalidated reaches the audio core.** Records are canonicalised then strictly validated (finite,
  within the editor's own limits — `VoiceEdit::limits()` — valid choices, recipe engine only with a recipe base)
  on upload, on boot, and again on every pad tap and audition.
- **Atomic.** A bank is streamed to a temp file and renamed only after every record and the CRC are written. A
  refusal, storage error, abort, timeout or pulled cable leaves the live bank untouched.
- **Floats.** NaN/∞ are rejected by bit pattern (`patchfields::finiteFloat`), never `std::isfinite`, because the
  firmware is built with `-ffast-math`.
- **No heap in the hot path**; 4.5 KB of static RAM; all work on Core 0 in bounded slices (≤ 600 bytes read per
  pass, one reply per request).
- **Songs and presets are separate.** The song file keeps each voice's full values, so deleting or replacing a
  user preset never changes what a saved song sounds like.

### 5.10 Tests

```bash
# Firmware logic (host): record, bank, store, validation, protocol, session, browser, generated resources
cmake -B build_test_ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build_test_ninja --parallel
./build_test_ninja/tests/pico2seq_tests "[userpreset],[presetlink],[browser],[resources]"

# Windows program logic: needs the .NET 8 SDK; runs on Linux too
dotnet test tools/PresetStudio/PresetStudio.Core.Tests
```

`preset_link_sim` (built with the host suites) runs the firmware's own parser, session, store and validator over
stdin/stdout with a file for flash. The C# tests start it and drive the real client — hello, push/pull,
restarts, refusals, auto-detect, live audition — against it, and skip with a note if it is not built. Point
`PICO2SEQ_LINK_SIM` at the executable to use another location.

What is **not** covered by a host test, by design: the LittleFS binding (`UserPresetStorage.cpp`), the Serial
glue (`PresetLinkService.cpp`), LED/OLED drawing, and the WPF windows. The firmware sources do compile
(`arduino-cli`, see [CLAUDE.md](../CLAUDE.md)); the XAML is checked by `XamlSanityTests` (every resource key,
binding name and event handler resolves) but has to be seen on Windows to be believed.

### 5.11 Building and publishing the Windows program

```powershell
# run it
dotnet run --project tools/PresetStudio/PresetStudio.App

# a single-file exe for another PC (needs the .NET 8 Desktop Runtime there)
scripts/build_preset_studio.ps1
# or fully self-contained (bigger, nothing to install):
scripts/build_preset_studio.ps1 -SelfContained
```

The solution (`tools/PresetStudio/PresetStudio.sln`) builds on any OS with the .NET 8 SDK
(`EnableWindowsTargeting`), but only runs on Windows.
