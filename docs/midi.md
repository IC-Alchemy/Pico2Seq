# MIDI Module Documentation

## Overview

The `src/midi/` subsystem handles USB MIDI communication for Pico2Seq using **Adafruit TinyUSB** and the Arduino MIDI library (`midi::SerialMIDI<Adafruit_USBD_MIDI>`).

The MIDI subsystem provides:
1. **Monophonic MIDI Note-On / Note-Off generation** synchronized with sequencer gate timing.
2. **Continuous Controller (CC) output** for real-time synthesis parameter changes.
3. **Realtime MIDI Clock transmission** (`Clock`, `Start`, `Stop`) synchronized with `uClock` at 24 PPQN.

---

## 4-Voice Asymmetry: 2-Voice MIDI vs 4-Voice Audio

Pico2Seq features 4 internal polyphonic synthesizer voices (`VoiceSystem::MAX_VOICES = 4`), but exhibits an architectural asymmetry between internal audio synthesis and external MIDI routing (the former hardware gate pin outputs were removed when I2S took over their GPIOs):

| Voice Index | Voice Name | Audio Synthesis | MIDI Note / CC Output |
|---|---|---|---|
| **Voice 0** | Voice 1 | Yes (Core 1 @ 48kHz) | **Yes** (Channel 1, CC 71–74) |
| **Voice 1** | Voice 2 | Yes (Core 1 @ 48kHz) | **Yes** (Channel 1, CC 75–78) |
| **Voice 2** | Voice 3 | Yes (Core 1 @ 48kHz) | **No** (Audio-only synthesis) |
| **Voice 3** | Voice 4 | Yes (Core 1 @ 48kHz) | **No** (Audio-only synthesis) |

> **Key Architectural Constraint:**
> `MidiNoteManager` explicitly tracks **only Voices 0 and 1** (`voice1Tracker` and `voice2Tracker`). Voices 2 and 3 are internal audio synthesis voices and do not emit MIDI note events, CC messages, or hardware gate triggers.

---

## MIDI Continuous Controller (CC) Mappings

All CC messages are transmitted on **MIDI Channel 1** (`CC_MIDI_CHANNEL = 1`). External DAWs and synthesizers differentiate voices through discrete CC number ranges:

| Parameter | Voice 1 (Voice 0) CC | Voice 2 (Voice 1) CC | Range | Resolution |
|---|---|---|---|---|
| **Octave Offset** | **CC 71** | **CC 75** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Decay Time** | **CC 72** | **CC 76** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Attack Time** | **CC 73** | **CC 77** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Filter Cutoff** | **CC 74** | **CC 78** | 0–127 | Linear map (`0.0f`–`1.0f`) |

*(CC 74 is the standard MIDI specification controller for Sound Brightness / Filter Cutoff).*

### Anti-Spam & Rate Limiting (`MidiCCConfig.h`)
- **Minimum Interval:** `CC_MIN_INTERVAL_MS = 10` (transmissions spaced by at least 10 ms per parameter).
- **Change Detection:** `CC_CHANGE_DETECTION_ENABLED = true` (CC messages are transmitted only when the quantized 7-bit MIDI value actually changes).
- **State Array:** `CCParameterState ccStates[2][4]` tracks timestamp and value state across both MIDI voices and all 4 parameters.

---

## Realtime MIDI Clock Transmission

Pico2Seq acts as a USB MIDI master clock source. Realtime clock messages are generated on **Core 0** directly from `uClock` timer callbacks (the stock library's alarm ISR fires on Core 0) in `Pico2Seq.ino` — note that `onSync24Callback` sends from ISR context:

```cpp
// 24 PPQN Clock Tick Callback
void onSync24Callback(uint32_t tick) {
    usb_midi.sendRealTime(midi::Clock);
}

// Sequencer Playback Start
void onClockStart() {
    usb_midi.sendRealTime(midi::Start);
    seq1.start();
    seq2.start();
    seq3.start();
    seq4.start();
    isClockRunning = true;
}

// Sequencer Playback Stop
void onClockStop() {
    usb_midi.sendRealTime(midi::Stop);
    seq1.stop();
    seq2.stop();
    seq3.stop();
    seq4.stop();
    midiNoteManager.onSequencerStop();
    isClockRunning = false;
}
```

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

    // CC Transmission
    void updateParameterCC(uint8_t voiceId, ParamId paramId, float value);
    void sendCCIfChanged(uint8_t voiceId, ParamId paramId, float value);
    void sendCC(uint8_t ccNumber, uint8_t value, uint8_t channel = 1);
    uint8_t getParameterCCNumber(uint8_t voiceId, ParamId paramId);
    uint8_t scaleParameterToMidi(ParamId paramId, float value);
};

extern MidiNoteManager midiNoteManager;
extern midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi;
```

---

## Dual-Core Execution Model

- **Core 0 Execution:** All MIDI polling (`usb_midi.read()`), note transmission (`noteOn`/`noteOff`), CC updates, and clock broadcasts (`sendRealTime`) run on **Core 0** — note that the 24-PPQN clock sends and step-time note/CC sends from the uClock callback chain happen in **ISR context** and share the TinyUSB endpoint with `tud_task` on the same core (packets can be dropped under contention; a count-and-drain refactor is the known hardening path).
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