# MIDI Module Documentation

## Overview

The `src/midi/` subsystem retains the **internal** note-lifecycle state machine for
Pico2Seq. It performs no MIDI I/O of any kind.

**USB MIDI was removed entirely 2026-09-06** (commit `bc66c8e`). The firmware
transmits no MIDI — no notes, no CC, no clock — and receives none either: there
is no `MIDI.read`/`usb_midi.read` call anywhere in `src/`, and no note-in
handler. USB carries power and the TinyUSB CDC serial console only. The
Arduino MIDI Library dependency (`<MIDI.h>`, `midi::SerialMIDI<Adafruit_USBD_MIDI>`)
was dropped; the `usb_midi` interface object no longer exists. `MidiManager.h`
still includes `<Adafruit_TinyUSB.h>` only because the sketch's TinyUSB CDC
stack requires it. `MidiNoteManager` is retained for internal gate/note
lifecycle bookkeeping; every transmission path is a stub. Transmission
references below are historical.

The MIDI subsystem provides:
1. **Internal monophonic note lifecycle state** synchronized with sequencer gate timing (no transmission).
2. **No MIDI realtime clock output** — uClock drives only the internal sequencer.

---

## 4-Voice Asymmetry: 2-Voice MIDI vs 4-Voice Audio

Pico2Seq features 4 internal polyphonic synthesizer voices (`VoiceSystem::MAX_VOICES = 4`), with an architectural asymmetry between the four audio voices and the two voices that have MIDI note/gate bookkeeping (the former hardware gate pin outputs were removed when I2S took over their GPIOs):

| Voice Index | Voice Name | Audio Synthesis | Internal Note/CC Bookkeeping (`MidiNoteManager`) |
|---|---|---|---|
| **Voice 0** | Voice 1 | Yes (Core 1 @ 48kHz) | **Yes** (voice1Tracker, CC 71–74 state) |
| **Voice 1** | Voice 2 | Yes (Core 1 @ 48kHz) | **Yes** (voice2Tracker, CC 75–78 state) |
| **Voice 2** | Voice 3 | Yes (Core 1 @ 48kHz) | **No** (Audio-only synthesis) |
| **Voice 3** | Voice 4 | Yes (Core 1 @ 48kHz) | **No** (Audio-only synthesis) |

> **Key Architectural Constraint:**
> `MidiNoteManager` explicitly tracks **only Voices 0 and 1** (`voice1Tracker` and `voice2Tracker`). Voices 2 and 3 are internal audio synthesis voices and have no note tracker, no CC parameter state, and no hardware gate triggers. Nothing is transmitted on any channel — the channel/CC numbers below exist only as bookkeeping identifiers inside the retained state machine.

---

## CC Number Registry (Vestigial, `MidiCCConfig.h`)

The historical CC map is retained unchanged in `MidiCCConfig.h` as bookkeeping
for the (stubbed) transmission pipeline. No CC bytes leave the device.

| Parameter | Voice 1 (Voice 0) CC | Voice 2 (Voice 1) CC | Range | Resolution |
|---|---|---|---|---|
| **Octave Offset** | **CC 71** | **CC 75** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Decay Time** | **CC 72** | **CC 76** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Attack Time** | **CC 73** | **CC 77** | 0–127 | Linear map (`0.0f`–`1.0f`) |
| **Filter Cutoff** | **CC 74** | **CC 78** | 0–127 | Linear map (`0.0f`–`1.0f`) |

*(CC 74 is the standard MIDI specification controller for Sound Brightness / Filter Cutoff.)*

### Anti-Spam & Rate Limiting (`MidiCCConfig.h`) — now vestigial
`StepPlayback.cpp` still calls `midiNoteManager.updateParameterCC(...)`, so the
change-detection/rate-limiting machinery still runs — it just gates a stub:
- **Minimum Interval:** `CC_MIN_INTERVAL_MS = 10` (would space transmissions by at least 10 ms per parameter).
- **Change Detection:** `CC_CHANGE_DETECTION_ENABLED = true` (CC would be "sent" only when the quantized 7-bit MIDI value actually changes).
- **State Array:** `CCParameterState ccStates[2][4]` tracks timestamp and value state across both tracked voices and all 4 parameters.

---

## MIDI Clock: Not Transmitted

Pico2Seq does **not** act as a USB MIDI master clock. The uClock tempo clock is
internal-only: it drives the four sequencers and the internal 480-PPQN timing
stream, but no `Clock`, `Start`, or `Stop` realtime bytes are sent anywhere
(the `setOnSync24` hook and the clock-sends drain were removed 2026-09-06).
`onClockStart`/`onClockStop` (now in `src/app/ClockService.cpp`) still
start/stop all four sequencers, and `onClockStop` runs
`midiNoteManager.onSequencerStop()` for clean note state — they just send no MIDI.

There is no `usb_midi` object at all anymore (the interface was removed). The
uClock ISR (core 0) only stages events into `ClockEvents` (`src/app/ClockService.cpp`):
sequencer steps go into the `steps` `SpscQueue<uint32_t, 16>` (overflow counted in
`droppedSteps`), and 480-PPQN ticks increment `ppqnTicksPending`.
`Application::update()` drains both in thread context: `processClockEvents()`
pops steps into `processSequencerStep()`, and `processPendingGateTicks()`
decrements `ppqnTicksPending` per tick, advancing `gateTick`,
`midiNoteManager.updateTiming(gateTick)`, `seq1/seq2.tickNoteDuration()`, and
`voiceSystem.tickAllGateTimers()`.

> **Known race, fix staged but not wired:** `ppqnTicksPending` is still a plain
> `volatile uint32_t` incremented in the ISR and decremented in the loop — the
> classic test-then-decrement lost-increment window is open. The tested
> replacement, `src/utils/PendingTickCounter.h` (atomic fetch_add /
> exchange-swap, covered by `tests/unit/test_pending_tick_counter.cpp`), is not
> yet used by `ClockService.cpp`.
---

## Note Lifecycle & Monophonic Tracking

`MidiNoteManager` enforces strict monophonic note tracking per tracked voice.
Call sites (all on core 0, thread context — the ISR only stages events):

- `noteOn(...)` / `setGateState(...)` / `updateParameterCC(...)` — from
  `processSequencerStep()` / `updateVoiceMIDI()` in `src/app/StepPlayback.cpp`,
  drained from the clock step queue by `ClockService::processClockEvents()`.
- `updateTiming(currentTick)` — from `ClockService::processPendingGateTicks()`,
  once per drained 480-PPQN tick (see the clock section above).
- `onSequencerStop()` — from `ClockService::onClockStop()`.

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

    // Gate synchronization & state queries
    void setGateState(uint8_t voiceId, bool gateActive, uint16_t gateDuration = 0);
    bool isGateActive(uint8_t voiceId) const;
    bool isNoteActive(uint8_t voiceId) const;
    int8_t getActiveNote(uint8_t voiceId) const;

    // Safety & transport cleanup
    void allNotesOff();
    void voiceReset(uint8_t voiceId);
    void emergencyStop();
    void onSequencerStop();
    void onModeSwitch();
    void onParameterChange(uint8_t voiceId);
    void onTempoChange();

    // Cross-thread safety (volatile updateInProgress flag)
    void beginAtomicUpdate(uint8_t voiceId);
    void endAtomicUpdate(uint8_t voiceId);

    // CC bookkeeping (transmission stubbed; rate limiting vestigial)
    void updateParameterCC(uint8_t voiceId, ParamId paramId, float value);
    void sendCCIfChanged(uint8_t voiceId, ParamId paramId, float value);
    void sendCC(uint8_t ccNumber, uint8_t value, uint8_t channel = 1);
    bool shouldTransmitCC(uint8_t voiceId, ParamId paramId, float value);
    uint8_t getParameterCCNumber(uint8_t voiceId, ParamId paramId);
    uint8_t scaleParameterToMidi(ParamId paramId, float value);
    void resetCCStates();
};

extern MidiNoteManager midiNoteManager;
// No usb_midi object exists — the USB MIDI interface was removed 2026-09-06.
```

---

## Dual-Core Execution Model

- **Core 0 Execution:** The USB CDC serial console runs on Core 0. No MIDI polling, transmission, or reception exists (USB MIDI removed 2026-09-06). The uClock ISR (also core 0) only stages events; it sends nothing.
- **Core 1 Isolation:** Core 1 runs purely audio synthesis DSP and I2S buffer filling. It never touches USB endpoints or MIDI state.
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
