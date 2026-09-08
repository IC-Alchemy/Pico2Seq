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

# ---------- Pico2Seq.ino ----------
flex_fix('Pico2Seq.ino', [
# globals + comment header
("""// --- MIDI & Clock ---
Adafruit_USBD_MIDI raw_usb_midi;
midi::SerialMIDI<Adafruit_USBD_MIDI> serial_usb_midi(raw_usb_midi);
midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi(serial_usb_midi);
Adafruit_MPR121 touchSensor = Adafruit_MPR121();""",
 """// --- Clock ---
// USB MIDI removed 2026-09-06: no MIDI interface, no note/CC/clock transmission.
// USB is used only for power and the TinyUSB CDC serial console (Serial).
Adafruit_MPR121 touchSensor = Adafruit_MPR121();"""),
# setup(): usb_midi.begin was the first statement
("""    // Initialize MIDI communication
    usb_midi.begin(MIDI_CHANNEL_OMNI);

    delay(100);""",
 """    // (USB MIDI removed 2026-09-06 — no usb_midi.begin; Serial below is TinyUSB CDC.)

    delay(100);"""),
# loop(): usb_midi.read
("""    // Process MIDI input/output
    freezeWatchdogFeed(FW_LOOP_USB_READ);
    usb_midi.read();
""",
 """    // (USB MIDI removed 2026-09-06 — nothing to read; breadcrumb kept for the
    //  freeze post-mortem phase map.)
    freezeWatchdogFeed(FW_LOOP_USB_READ);
"""),
])

# ---------- includes.h ----------
flex_fix('includes.h', [
("""#include <Adafruit_MPR121.h> // MAKE SURE TO ENABLE AUTOCONFIG IN MPR121.H
#include <MIDI.h>
#include <Adafruit_TinyUSB.h>""",
 """#include <Adafruit_MPR121.h> // MAKE SURE TO ENABLE AUTOCONFIG IN MPR121.H
// <MIDI.h> / USB MIDI removed 2026-09-06. Adafruit_TinyUSB.h stays: it provides
// the TinyUSB CDC serial console (Serial); usbstack=tinyusb is still required.
#include <Adafruit_TinyUSB.h>"""),
])

# ---------- src/midi/MidiManager.h ----------
s = load('src/midi/MidiManager.h')
s2 = re.sub(r'#include <MIDI\.h>\r?\n', '', s, count=1)
assert s2 != s, 'mm.h include'
s = s2
old = """// USB MIDI interface
extern midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi;
"""
pat = re.compile(r'\r?\n'.join(re.escape(l) for l in old.split('\n')))
m = pat.search(s)
assert m, 'mm.h extern'
nl = '\r\n' if '\r\n' in m.group(0) else '\n'
s = s[:m.start()] + nl.join(["// (USB MIDI interface removed 2026-09-06 — no transmission."]) + s[m.end():]
save('src/midi/MidiManager.h', s)
print('ok src/midi/MidiManager.h')

# ---------- src/midi/MidiManager.cpp ----------
s = load('src/midi/MidiManager.cpp')
reps = [
("extern midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi;\n",
 "// (usb_midi extern removed — USB MIDI transmission removed 2026-09-06.)\n"),
("""    if (midiNote >= 0 && midiNote <= 127)
    {
        usb_midi.sendNoteOn(midiNote, velocity, channel);
    }""",
 """    // USB MIDI removed 2026-09-06 — note lifecycle state still tracked, nothing sent.
    (void)velocity;"""),
("""    if (midiNote >= 0 && midiNote <= 127)
    {
        usb_midi.sendNoteOff(midiNote, 0, channel);
    }""",
 """    // USB MIDI removed 2026-09-06 — nothing sent."""),
]
for old, new in reps:
    pat = re.compile(r'\r?\n'.join(re.escape(l) for l in old.split('\n')))
    m = pat.search(s)
    assert m, ('mm.cpp', old[:60])
    nl = '\r\n' if '\r\n' in m.group(0) else '\n'
    s = s[:m.start()] + nl.join(new.split('\n')) + s[m.end():]
# the three all-notes/all-sound CC sends + the CC sender
s = s.replace("usb_midi.sendControlChange(123, 0, 1); // All Notes Off on channel 1",
              "// (All Notes Off send removed with USB MIDI 2026-09-06.)")
s = s.replace("usb_midi.sendControlChange(120, 0, 1); // All Sound Off",
              "// (All Sound Off send removed with USB MIDI 2026-09-06.)")
s = s.replace("usb_midi.sendControlChange(123, 0, 1); // All Notes Off",
              "// (All Notes Off send removed with USB MIDI 2026-09-06.)")
s = s.replace("""    // Send MIDI CC message via USB
    usb_midi.sendControlChange(ccNumber, value, channel);""",
              """    // USB MIDI removed 2026-09-06 — CC values are validated but nothing is sent.
    (void)ccNumber; (void)value; (void)channel;""")
assert 'usb_midi' not in s, 'mm.cpp leftover'
save('src/midi/MidiManager.cpp', s)
print('ok src/midi/MidiManager.cpp')

# ---------- FreezeWatchdog.h label ----------
flex_fix('src/utils/FreezeWatchdog.h', [
('case FW_LOOP_USB_READ:   return "loop: usb_midi.read";',
 'case FW_LOOP_USB_READ:   return "loop: housekeeping (midi read removed)";'),
])
