# VoiceSystem Architecture Documentation

## 1. Overview

`VoiceSystem` (`src/voice/VoiceSystem.h`) holds voice IDs and control-core
`VoiceState` snapshots for four voices. It is not an audio-state mirror or a
second note-duration engine. All firmware voice indices are 0-based (0–3).

The redundant gate flags, gate timers and `GateTimer` type have been removed.
Each `Sequencer` owns its note lifecycle and duration; all four voices use the
same path, without the former two-voice MIDI bookkeeping.

## 2. Core Structure & Definition

```cpp
struct VoiceSystem {
    static constexpr uint8_t MAX_VOICES = 4;
    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};
    VoiceState voiceStates[MAX_VOICES];

    uint8_t getVoiceId(uint8_t voiceIndex) const;
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId);
    VoiceState& getVoiceState(uint8_t voiceIndex);
    const VoiceState& getVoiceState(uint8_t voiceIndex) const;
};

extern VoiceSystem voiceSystem;
```

Use the accessors rather than indexing the arrays directly. An invalid ID read
returns `0`, an invalid ID write is ignored, and an invalid state index selects
voice 0. These bounds checks do not change the valid voice range.

## 3. Ownership and Routing

- **Core 0** owns mutable control snapshots. Step playback, note-duration expiry
  and UI edits update requested state here.
- **Core 1** owns applied DSP state. `VoiceManager::updateVoiceState()` stages
  controls through each voice's bounded SPSC queue; audio does not read mutable
  `VoiceSystem` snapshots directly. See [cross-core ownership](architecture.md#3-cross-core-ownership-and-bounded-queues).
- **`AppState::sequencers`** is the immutable, non-owning routing table in voice
  order. The concrete `seq1`..`seq4` objects are still constructed in
  `src/app/AppState.cpp`; the table does not allocate or own them.
- **`UIState`** owns selected voice, preset indices and UI modes. Settings
  predicates derive from `settingsMode` and `currentSubMode`; they are not
  independent flags that callers must synchronize.

## 4. Step and Duration Flow

The uClock ISR only enqueues step numbers and stages PPQN ticks. Core 0's
ordinary control loop drains them:

1. `processSequencerStep()` advances the sequencers through the application
   routing table, applies per-voice controls, stores the resulting snapshots
   and publishes them through `VoiceManager`.
2. `ClockService::processPendingGateTicks()` calls
   `Sequencer::tickNoteDuration()` for each voice. This is the sole note-duration
   authority. On expiry it updates the supplied `VoiceState` to gate-off; the
   clock service immediately publishes that state to `VoiceManager`, so release
   does not wait for the next sequencer step.
3. Transport stop clears sequencer note activity and publishes gate-off states.
   There is no separate `VoiceSystem` countdown or MIDI tracker to stop.

Gate length and Gate track length are different: GateLength determines a note's
PPQN duration; Gate Sequence Length Mode edits the number of steps in the Gate
track. Both remain sequencer behavior, not `VoiceSystem` behavior.

## 5. API Reference Summary

| Method | Returns | Invalid index behavior |
|---|---|---|
| `getVoiceId(voiceIndex)` | `uint8_t` | Returns `0` |
| `setVoiceId(voiceIndex, id)` | `void` | Ignores write |
| `getVoiceState(voiceIndex)` | `VoiceState&` | Returns voice 0 snapshot |
| `getVoiceState(voiceIndex) const` | `const VoiceState&` | Returns voice 0 snapshot |

## Related Documentation

- [Firmware structure](firmware-structure.md) — application routing and lifecycle
- [Sequencer](sequencer.md) — polymetric tracks, gate duration and slide
- [Voice](voice.md) — synthesis and queued control updates
- [MIDI status](midi.md) — removed firmware module and retained portable hooks
