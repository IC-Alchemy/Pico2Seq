import io, re

def load(p):
    return io.open(p, 'rb').read().decode('utf-8')

def save(p, s):
    io.open(p, 'wb').write(s.encode('utf-8'))

def flex_fix(p, reps):
    s = load(p)
    for old, new in reps:
        lines = old.split('\n')
        pat = re.compile(r'\r?\n'.join(re.escape(l) for l in lines))
        m = pat.search(s)
        assert m, (p, old[:70])
        nl = '\r\n' if '\r\n' in m.group(0) else '\n'
        s = s[:m.start()] + nl.join(new.split('\n')) + s[m.end():]
    save(p, s)
    print('ok', p)

# ---------- AGENTS.md ----------
flex_fix('AGENTS.md', [
("""- **Core 0** (`setup()`/`loop()` in `Pico2Seq.ino`): everything except the audio hot path —
  MIDI I/O, TMAG5273 magnetic encoder""",
 """- **Core 0** (`setup()`/`loop()` in `Pico2Seq.ino`): everything except the audio hot path —
  the USB CDC serial console (USB MIDI removed entirely 2026-09-06), TMAG5273 magnetic encoder"""),
("`README.md` (Adafruit_MPR121, Adafruit_SH110X, Adafruit_TinyUSB, FastLED, Adafruit_VL53L1X,\nMIDI — plus **stock uClock 2.2.1 from the library manager**",
 "`README.md` (Adafruit_MPR121, Adafruit_SH110X, Adafruit_TinyUSB, FastLED, Adafruit_VL53L1X —\nplus **stock uClock 2.2.1 from the library manager**"),
("""Matrix/TMAG5273/VL53L1X/MIDI input  (Core 0)""",
 """Matrix/TMAG5273/VL53L1X input  (Core 0)"""),
])

# ---------- docs/midi.md ----------
flex_fix('docs/midi.md', [
("""The MIDI subsystem provides:
1. **Monophonic MIDI Note-On / Note-Off generation** synchronized with sequencer gate timing.
2. **Continuous Controller (CC) output** for real-time synthesis parameter changes.
3. **No MIDI realtime clock output** — uClock drives only the internal sequencer; no `Clock`, `Start`, or `Stop` bytes are transmitted (removed 2026-09-06).""",
 """**USB MIDI was removed entirely 2026-09-06.** The firmware transmits no MIDI at all —
no notes, no CC, no clock. USB carries power and the TinyUSB CDC serial console only.
The `MidiNoteManager` state machine is retained for internal gate/note lifecycle
bookkeeping, but every transmission path is a stub; transmission references below are
historical.

The MIDI subsystem provides:
1. **Internal monophonic note lifecycle state** synchronized with sequencer gate timing (no transmission).
2. **No MIDI realtime clock output** — uClock drives only the internal sequencer."""),
("""All remaining `usb_midi` traffic (note on/off and CC for voices 0–1) originates
from `loop()`/UI-handler **thread context on Core 0** — the same core TinyUSB's
`tud_task` runs on — so the MIDI endpoint keeps exactly one producer. The uClock
ISR (also core 0) only stages events (the `stepQueue` `SpscQueue` +
`ppqnTicksPending`), which `processClockEvents()` drains in `loop()`.""",
 """There is no `usb_midi` traffic at all anymore (the interface was removed). The uClock
ISR (core 0) only stages events (the `stepQueue` `SpscQueue` + `ppqnTicksPending`),
which `processClockEvents()` drains in `loop()`."""),
("- **Core 0 Execution:** All MIDI polling (`usb_midi.read()`) and every transmission (`noteOn`/`noteOff` note and CC output) run in **thread context on Core 0** — the same core TinyUSB's `tud_task` runs on, so the MIDI endpoint keeps exactly one producer. The uClock ISR (also core 0) only stages events; it sends no MIDI.",
 "- **Core 0 Execution:** The USB CDC serial console runs on Core 0. No MIDI polling or transmission exists (USB MIDI removed 2026-09-06). The uClock ISR (also core 0) only stages events; it sends nothing."),
])

# ---------- docs/VoiceSystem.md ----------
flex_fix('docs/VoiceSystem.md', [
("   - **Voices 0 and 1**: Fully equipped with USB MIDI note on/off tracking and `GateTimer` PPQN countdowns.",
 "   - **Voices 0 and 1**: Fully equipped with internal note on/off tracking (`MidiNoteManager`; USB MIDI transmission removed 2026-09-06) and `GateTimer` PPQN countdowns."),
])

# ---------- docs/scales.md ----------
flex_fix('docs/scales.md', [
("-> USB MIDI Note On (0-127 clamped)", "-> Internal MIDI note (0-127 clamped)"),
])

# ---------- docs/sequencer.md ----------
flex_fix('docs/sequencer.md', [
("   - Voices 0 and 1: Full support for audio synthesis and USB MIDI output via `MidiNoteManager`.",
 "   - Voices 0 and 1: Audio synthesis plus the internal note-lifecycle state machine via `MidiNoteManager` (USB MIDI transmission removed 2026-09-06 — nothing is sent)."),
])

# ---------- skill: SKILL.md invariant ----------
flex_fix('.agents/skills/pico2seq-codebase/SKILL.md', [
("""   (`stepQueue` `SpscQueue<uint32_t, 16>` + `ppqnTicksPending`); `loop()` drains
   them in thread context via `processClockEvents()` — never put `usb_midi.send*` or
   sequencer/midiNoteManager work back into ISR context.""",
 """   (`stepQueue` `SpscQueue<uint32_t, 16>` + `ppqnTicksPending`); `loop()` drains
   them in thread context via `processClockEvents()` — never put sequencer or
   midiNoteManager work back into ISR context. (USB MIDI was removed entirely
   2026-09-06; USB carries power + the CDC serial console only.)"""),
])

# ---------- skill: references/architecture.md ----------
flex_fix('.agents/skills/pico2seq-codebase/references/architecture.md', [
("| USB MIDI (`usb_midi.begin()` first, then `usb_midi.read()` each pass), MIDI note/CC out via `MidiNoteManager` | `setup1()`: `delay(100)`, audio format/pool setup, `setupI2SAudio()` |",
 "| USB CDC serial console (`Serial.begin()` early in `setup()`; the `usb_midi` interface was removed 2026-09-06), internal note lifecycle via `MidiNoteManager` | `setup1()`: `delay(100)`, audio format/pool setup, `setupI2SAudio()` |"),
("  `updateVoiceMIDI()` with `midiNoteManager.noteOn()` → `usb_midi.sendNoteOn`).",
 "  `updateVoiceMIDI()` with `midiNoteManager.noteOn()` — no transmission; USB MIDI was removed 2026-09-06)."),
("  `midiNoteManager.onSequencerStop()`; no usb_midi traffic anymore).",
 "  `midiNoteManager.onSequencerStop()`; no usb_midi interface exists anymore)."),
])

# ---------- skill: references/audio-midi.md MIDI section ----------
flex_fix('.agents/skills/pico2seq-codebase/references/audio-midi.md', [
("""  Stack: `Adafruit_USBD_MIDI` → `midi::SerialMIDI<>` → `midi::MidiInterface`
  (`Pico2Seq.ino:24-26`), `usb_midi.begin(OMNI)` first in `setup()`.""",
 """  **USB MIDI was removed entirely 2026-09-06** — no `Adafruit_USBD_MIDI`, no
  `<MIDI.h>`, no transmission. The `MidiNoteManager` state machine remains for
  internal gate/note lifecycle bookkeeping (voices 0–1 only), but every send is a
  stub. USB carries power + the TinyUSB CDC serial console only
  (`usbstack=tinyusb` is still required for `Serial`)."""),
("""- `usb_midi.read()` runs every `loop()` but **no `setHandle*` callbacks are
  registered** — MIDI input is drained and unused. uClock is the clock master,
  but the firmware sends **no MIDI realtime clock output** (no Clock /
  Start / Stop bytes — removed 2026-09-06; it cannot sync external gear).
  Everything touching `usb_midi` runs in `loop()`/UI-handler thread context on
  core 0 — the same core as TinyUSB's `tud_task` (initialized there in
  `main()`), so the endpoint keeps a single producer.""",
 """- No MIDI input or output exists. uClock is the internal clock master; the
  firmware sends nothing (no notes, CC, Clock/Start/Stop)."""),
])

# ---------- playground.html ----------
flex_fix('playground.html', [
("  {id:'usbmidi',label:'USB MIDI',           sub:'src/midi/ · TinyUSB',               x:290,y:928, layer:'out'},",
 "  {id:'usbmidi',label:'CDC Serial',          sub:'TinyUSB CDC · 115200 (no MIDI)',    x:290,y:928, layer:'out'},"),
("Also <code>usb_midi.read()</code> for incoming MIDI.", "USB MIDI was removed — the pass only runs the USB CDC serial console."),
("usbmidi:{p:['USB MIDI via Adafruit_TinyUSB. Out: notes and CCs for voices 0/1 via MidiNoteManager — <b>no MIDI clock</b> (Clock/Start/Stop transmission was removed). In: <code>usb_midi.read()</code> each loop() pass — the expansion point for MIDI-in features.']},",
 "usbmidi:{p:['TinyUSB CDC serial console (115200 baud) — diagnostics output. USB MIDI was removed entirely 2026-09-06: no notes, CC, or clock are transmitted; <code>MidiNoteManager</code> survives as the internal note-lifecycle state machine only.']},"),
("  {id:'e34',f:'midimgr',t:'usbmidi',ty:'data',label:'notes · CC'},",
 "  {id:'e34',f:'midimgr',t:'usbmidi',ty:'data',label:'internal state (nothing sent)'},"),
])

# ---------- docs/interactive_manual.html ----------
flex_fix('docs/interactive_manual.html', [
("Only <b>Voices 1 and 2</b> have USB MIDI note/gate output and gate timers wired. Voices 3 and 4 are synthesis-only and do not emit external MIDI notes.",
 "Only <b>Voices 1 and 2</b> have the internal note-lifecycle state machine and gate timers wired. Voices 3 and 4 are synthesis-only. USB MIDI transmission was removed 2026-09-06 — nothing is emitted."),
])
