# MIDI Module Documentation

## Overview

The `src/midi/` subsystem retains the internal gate/note lifecycle state machine from the
former USB MIDI stack (Adafruit TinyUSB; the Arduino MIDI library is no longer used).

**USB MIDI was removed entirely 2026-09-06.** The firmware transmits no MIDI at all —
no notes, no CC, no clock. USB carries power and the TinyUSB CDC serial console only.
The `MidiNoteManager` state machine is retained for internal gate/note lifecycle
bookkeeping, but every transmission path is a stub; transmission references below are
historical.

The MIDI subsystem provides:
1. **Internal monophonic note lifecycle state** synchronized with sequencer gate timing (no transmission).
2. **No MIDI realtime clock output** — uClock drives only the internal sequencer.

---

## 4-Voice Asymmetry: 2-Voice Note Lifecycle vs 4-Voice Audio

Pico2Seq features 4 internal polyphonic synthesizer voices (`VoiceSystem::MAX_VOICES = 4`), and the old 2-voice MIDI/hardware-gate asymmetry survives only as internal bookkeeping (the former hardware gate pin outputs were removed when I2S took over their GPIOs, and USB MIDI transmission was removed 2026-09-06):

| Voice Index | Voice Name | Audio Synthesis | MIDI Output | Internal Note Lifecycle |
|---|---|---|---|---|
| **Voice 0** | Voice 1 | Yes (Core 1 @ 48kHz) | **None** | **Yes** (`voice1Tracker`) |
| **Voice 1** | Voice 2 | Yes (Core 1 @ 48kHz) | **None** | **Yes** (`voice2Tracker`) |
| **Voice 2** | Voice 3 | Yes (Core 1 @ 48kHz) | **None** | No |
| **Voice 3** | Voice 4 | Yes (Core 1 @ 48kHz) | **None** | No |

> **Key Architectural Constraint:**
> `MidiNoteManager` explicitly tracks **only Voices 0 and 1** (`voice1Tracker` and `voice2Tracker`). Voices 2 and 3 are internal audio synthesis voices with no note bookkeeping. Nothing is transmitted anywhere — the trackers drive the software gate lifecycle only.

---

## MIDI Continuous Controller (CC) Mappings — dormant

The CC number tables below are **historical**: the constants and methods survive in
`MidiCCConfig.h` / `MidiManager.h`, but no CC message has been transmitted since USB MIDI
was removed 2026-09-06 (all send paths are stubs and nothing calls them from the firmware).

Formerly all CC messages were transmitted on **MIDI Channel 1** (`CC_MIDI_CHANNEL = 1`). External DAWs and synthesizers differentiated voices through discrete CC number ranges:

| Parameter | Voice 1 (Voice 0) CC | Voice 2 (Voice 1) CC | Range | Resolution |
|---|---|---|---|---|
| **Octave Offset** | **CC 71** | **CC 75** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Decay Time** | **CC 72** | **CC 76** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Attack Time** | **CC 73** | **CC 77** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Filter Cutoff** | **CC 74** | **CC 78** | 0–127 | Linear map (`0.0f`–`1.0f`) |

*(CC 74 is the standard MIDI specification controller for Sound Brightness / Filter Cutoff.)*

### Anti-Spam & Rate Limiting (`MidiCCConfig.h`, dormant)
- **Minimum Interval:** `CC_MIN_INTERVAL_MS = 10` (transmissions were spaced by at least 10 ms per parameter).
- **Change Detection:** `CC_CHANGE_DETECTION_ENABLED = true` (CC messages were transmitted only when the quantized 7-bit MIDI value actually changed).
- **State Array:** `CCParameterState ccStates[2][4]` tracks timestamp and value state across both MIDI voices and all 4 parameters.

---

## MIDI Clock: Not Transmitted

Pico2Seq does **not** act as a USB MIDI master clock. The uClock tempo clock is
internal-only: it drives the four sequencers and the internal 480-PPQN timing
stream, but no `Clock`, `Start`, or `Stop` realtime bytes are sent over USB MIDI
(the `setOnSync24` hook and the clock-sends drain were removed 2026-09-06).
`onClockStart`/`onClockStop` still start/stop all four sequencers and run
`midiNoteManager.onSequencerStop()` for clean note state — they just send no MIDI.

There is no `usb_midi` traffic at all anymore (the interface was removed). The uClock
ISR (core 0) only stages events (the `stepQueue` `SpscQueue` + `ppqnTicksPending`),
which `processClockEvents()` drains in `loop()`.
---

## Note Lifecycle & Monophonic Tracking

`MidiNoteManager` enforces strict monophonic note tracking per MIDI voice:

```
           [ Step Trigger / noteOn() ]
                        |
                        v
          +---------------------------+
          | Active Note Check         |
          | (If previous note active, |
          |  send immediate noteOff)  |
          +-------------+-------------+
                        |
                        v
          +---------------------------+
          | sendMidiNoteOn(note, vel) |
          | Set gateStartTick + dur   |
          +-------------+-------------+
                        |
        [ Clock tick: updateTiming() ]
                        |
                        v
          +---------------------------+
          | Gate Expired?             |
          | tick >= start + duration  |
          +-------------+-------------+
                        | (yes)
                        v
          +---------------------------+
          | sendMidiNoteOff(note)     |
          | State -> INACTIVE         |
          +---------------------------+
```

### Data Structures (`src/midi/MidiManager.h`)
```cpp
struct MidiNoteTracker {
    volatile int8_t activeMidiNote = -1; // -1 = none
    volatile uint8_t activeVelocity = 0;
    volatile uint8_t activeChannel = 1;
    volatile MidiNoteState state = MidiNoteState::INACTIVE;
    volatile bool gateActive = false;
    volatile uint16_t gateStartTick = 0;
    volatile uint16_t gateDurationTicks = 0;
    volatile uint16_t currentTick = 0;
    volatile bool updateInProgress = false;

    bool isNoteActive() const volatile;
    bool isGateExpired() const volatile;
    void reset() volatile;
};
```

---

## Software Architecture & Core API

```cpp
class MidiNoteManager {
public:
    MidiNoteManager();

    // Note Lifecycle (voiceId: 0 = Voice 1, 1 = Voice 2)
    void noteOn(uint8_t voiceId, int8_t midiNote, uint8_t velocity, uint8_t channel, uint16_t gateDuration);
    void noteOff(uint8_t voiceId);
    void updateTiming(uint16_t currentTick);

    // Gate & State Queries
    void setGateState(uint8_t voiceId, bool gateActive, uint16_t gateDuration = 0);
    bool isGateActive(uint8_t voiceId) const;
    bool isNoteActive(uint8_t voiceId) const;
    int8_t getActiveNote(uint8_t voiceId) const;

    // Safety & Transport Cleanup
    void allNotesOff();
    void emergencyStop();
    void onSequencerStop();
    void onModeSwitch();

    // CC Transmission (stubs — nothing is sent since USB MIDI removal 2026-09-06)
    void updateParameterCC(uint8_t voiceId, ParamId paramId, float value);
    void sendCCIfChanged(uint8_t voiceId, ParamId paramId, float value);
    void sendCC(uint8_t ccNumber, uint8_t value, uint8_t channel = 1);
    uint8_t getParameterCCNumber(uint8_t voiceId, ParamId paramId);
    uint8_t scaleParameterToMidi(ParamId paramId, float value);
};

extern MidiNoteManager midiNoteManager;
```

---

## Dual-Core Execution Model

- **Core 0 Execution:** The USB CDC serial console runs on Core 0. No MIDI polling or transmission exists (USB MIDI removed 2026-09-06). The uClock ISR (also core 0) only stages events; it sends nothing.
- **Core 1 Isolation:** Core 1 runs purely audio synthesis DSP and I2S buffer filling. It never blocks on USB MIDI endpoints.
- **Volatile Shared State:** Synchronization between the sequencer ticks and gate trackers uses `volatile` variables and atomic begin/end locks.

---

## File Structure

```
src/midi/
├── MidiCCConfig.h          # CC number definitions, channel, and rate-limiting constants
├── MidiManager.cpp         # MidiNoteManager implementation and CC routing
├── MidiManager.h           # MidiNoteManager interface, MidiNoteTracker & CCParameterState
└── README.md               # MIDI module overview
```

---

## Related Documentation

- [`docs/architecture.md`](architecture.md) — System dual-core split and lifecycle
- [`docs/VoiceSystem.md`](VoiceSystem.md) — VoiceSystem data structures and voice routing
- [`docs/sequencer.md`](sequencer.md) — Sequencer tick processing and polymetric tracks
- [`docs/scales.md`](scales.md) — Musical scale tables and MIDI note conversion