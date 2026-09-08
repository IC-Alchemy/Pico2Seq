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
  MIDI I/O, TMAG5273 magnetic encoder and VL53L1X distance sensor polling, MPR121 touch
  matrix scanning, `uClock` sequencer step ticking, LED matrix and OLED updates, UI state.""",
 """- **Core 0** (`setup()`/`loop()` in `Pico2Seq.ino`): everything except the audio hot path —
  the USB CDC serial console, TMAG5273 magnetic encoder and VL53L1X distance sensor
  polling, MPR121 touch matrix scanning, `uClock` sequencer step ticking, LED matrix and
  OLED updates, UI state. (USB MIDI was removed entirely 2026-09-06; USB carries power
  and the CDC console only.)"""),
("""the libraries listed in `README.md` (Adafruit_MPR121, Adafruit_SH110X, Adafruit_TinyUSB,
FastLED, Adafruit_VL53L1X, MIDI — plus **stock uClock 2.2.1 from the library manager**, which the firmware includes as
`<uClock.h>`)""",
 """the libraries listed in `README.md` (Adafruit_MPR121, Adafruit_SH110X, Adafruit_TinyUSB,
FastLED, Adafruit_VL53L1X — plus **stock uClock 2.2.1 from the library manager**, which the firmware includes as
`<uClock.h>`)"""),
("""Matrix/TMAG5273/VL53L1X/MIDI input  (Core 0)""",
 """Matrix/TMAG5273/VL53L1X input  (Core 0)"""),
])

# ---------- docs/midi.md top banner + provides list ----------
flex_fix('docs/midi.md', [
("""The MIDI subsystem provides:
1. **Monophonic MIDI Note-On / Note-Off generation** synchronized with sequencer gate timing.
2. **Continuous Controller (CC) output** for real-time synthesis parameter changes.
3. **No MIDI realtime clock output** — uClock drives only the internal sequencer; no `Clock`, `Start`, or `Stop` bytes are transmitted (removed 2026-09-06).""",
 """**USB MIDI was removed entirely 2026-09-06.** The firmware transmits no MIDI at all —
no notes, no CC, no clock. The USB connection carries power and the TinyUSB CDC serial
console only. The `MidiNoteManager` state machine is retained for internal gate/note
lifecycle bookkeeping, but every transmission path is a stub. The rest of this document
describes the retained internal state machine; all transmission references are historical.

The MIDI subsystem provides:
1. **Internal monophonic note lifecycle state** synchronized with sequencer gate timing (no transmission).
2. **No MIDI realtime clock output** — uClock drives only the internal sequencer."""),
])

# ---------- docs/manual.md: USB row + section 7 ----------
flex_fix('docs/manual.md', [
("| **USB** (Pico 2) | Power + USB MIDI (see §7) + serial diagnostics console at 115200 baud. |",
 "| **USB** (Pico 2) | Power + serial diagnostics console at 115200 baud (USB CDC). USB MIDI was removed 2026-09-06. |"),
])
s = load('docs/manual.md')
i = s.find('## 7. MIDI & connectivity')
assert i >= 0
j = s.find('\n## ', i + 5)
new7 = """## 7. MIDI & connectivity

**USB MIDI was removed entirely on 2026-09-06.** The firmware transmits no MIDI — no
notes, no CC, no clock. The USB-C connector carries power and the CDC serial
diagnostics console (115200 baud) only. Voices 1–2 (internal indices 0–1) still run the
internal gate/note lifecycle state machine (`MidiNoteManager`), but nothing is
transmitted. The Gate Input panel hole remains non-functional.
"""
s = s[:i] + new7 + s[j:]
save('docs/manual.md', s)
print('ok docs/manual.md')

# ---------- docs/architecture.md ----------
s = load('docs/architecture.md')
n = 0
for old, new in [
    ("USB MIDI (`usb_midi.begin()` first, then `usb_midi.read()` each pass), MIDI note/CC out via `MidiNoteManager`",
     "USB CDC serial console (`usb_midi` removed — no MIDI interface; `Serial.begin()` early in `setup()`), internal note lifecycle via `MidiNoteManager`"),
]:
    if old in s:
        s = s.replace(old, new, 1)
        n += 1
save('docs/architecture.md', s)
print('architecture spots:', n)
