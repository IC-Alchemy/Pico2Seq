# Sequencer Module Documentation

## 1. Overview & Architectural Principles

The Sequencer module is the core rhythmic and melodic engine of the Pico2Seq synthesizer. It implements a **4-voice polyphonic, polymetric step sequencer** where each synthesizer voice is driven by its own dedicated sequencer instance (`seq1`, `seq2`, `seq3`, `seq4`).

```
                              ┌────────────────────────────────────────────────────────┐
                              │                 Core 0 (Control Thread)                │
                              │                                                        │
                              │   uClock Timer ISR (90 BPM, 480 PPQN, Shuffle On)      │
                              │                           │ (every 16th note step)     │
                              │                           ▼                            │
                              │      onStepCallback()  (stage into stepQueue)          │
                              │                           │                            │
                              │                           ▼  loop(): processClockEvents()
                              │                  processSequencerStep()                │
                              │                           │                            │
                              │        ┌──────────────────┴──────────────────┐         │
                              │        ▼                                     ▼         │
                              │   Sensors / UIState                     4x Sequencers  │
                              │   (Distance, Encoder,                   (seq1..seq4)   │
                              │    Held Buttons)                             │         │
                              │        │                                     │         │
                              │        └──────────────┬──────────────────────┘         │
                              │                       ▼                                │
                              │             advanceSequencerStep()                     │
                              │             (UI Adapter in src/ui/)                    │
                              │                       │                                │
                              │                       ▼                                │
                              │             Sequencer::advanceStep()                   │
                              │             (Primitive core in pico2seq-core/)         │
                              │                       │                                │
                              │                       ▼                                │
                              │                  VoiceState                            │
                              │                       │                                │
                              │                       ▼                                │
                              │                  VoiceSystem                           │
                              │          (Gate Timers, MIDI NoteOn/Off)                │
                              │                       │                                │
                              └───────────────────────┼────────────────────────────────┘
                                                      │ Staged Parameters (thread-safe)
                                                      ▼
                              ┌────────────────────────────────────────────────────────┐
                              │                  Core 1 (Audio Thread)                 │
                              │                                                        │
                              │       VoiceManager::processAllVoices() @ 48kHz         │
                              │           Voice DSP Chain (Osc -> Filter -> ADSR)      │
                              │                       │                                │
                              │                       ▼                                │
                              │              fill_audio_buffer()                       │
                              │              I2S Stereo Audio Output                   │
                              └────────────────────────────────────────────────────────┘
```

### Key Architectural Principles

1. **Portable `pico2seq-core/` Isolation**:
   The sequencer logic in `src/pico2seq-core/sequencer/` (`Sequencer`, `ParameterManager`, `SequencerDefs.h`, `ShuffleTemplates.h`) is clean, portable C++ with **no dependency on `UIState` or UI types**. This allows the sequencer engine to be compiled and unit-tested on host machines via CMake (`tests/unit/test_sequencer.cpp`).
2. **UI Adapter Pattern (`advanceSequencerStep`)**:
   Because `Sequencer::advanceStep()` accepts only primitive types (integers, floats, booleans), the firmware bridges the rich `UIState` struct via the adapter function `advanceSequencerStep()` located in `src/ui/UIEventHandler.h` and `src/ui/UIEventHandler.cpp`.
3. **Polymetric Parameter Tracks**:
   Rather than advancing all synthesis parameters in lockstep, every parameter (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide) operates on an independent `ParameterTrack<64>` with its own step count (2–64 steps). This allows patterns such as a 16-step melody, an 8-step filter pattern, and a 5-step velocity cycle to run simultaneously on a single voice.
4. **Dual-Core Execution**:
   - **Core 0** runs `uClock` (timer ISR on core 0 — stock library). The ISR-stage-only `onStepCallback()` queues each 16th note; `loop()` drains it (`processClockEvents()` → `processSequencerStep()`), advancing all 4 sequencers, managing gate timers, and sending MIDI note-on/off events in thread context.
   - **Core 1** processes per-sample audio synthesis in `VoiceManager::processAllVoices()` using the parameter values generated by Core 0.
5. **4-Voice System Integration**:
   - Voices 0 and 1: Audio synthesis plus the internal note-lifecycle state machine via `MidiNoteManager` (USB MIDI transmission removed 2026-09-06 — nothing is sent).
   - Voices 2 and 3: Audio synthesis only (no external MIDI routing).

---

## 2. Polymetric Parameter Track Architecture

### 2.1 `ParamId` Enumeration

All automatable step parameters are identified by the `ParamId` enum class defined in `SequencerDefs.h`:

```cpp
enum class ParamId : uint8_t
{
  Note,       // 0 - Scale step index (0.0-21.0, maps to scale table)
  Velocity,   // 1 - Voice amplitude level (0.0-1.0)
  Filter,     // 2 - Filter cutoff frequency (0.0-1.0)
  Attack,     // 3 - Envelope attack time (0.0-1.0 seconds)
  Decay,      // 4 - Envelope decay time (0.0-1.0 seconds)
  Octave,     // 5 - Octave offset (0.0 = -1 oct / C2, 0.5 = 0 oct / C3, 1.0 = +1 oct / C4)
  GateLength, // 6 - Gate duration fraction (0.001-1.0 of step)
  Gate,       // 7 - Gate on/off state (boolean: 0.0 or 1.0)
  Slide,      // 8 - Portamento / glide enable (boolean: 0.0 or 1.0)
  Count       // 9 - Total parameter count for array sizing
};

constexpr uint8_t PARAM_ID_COUNT = static_cast<uint8_t>(ParamId::Count);
```

### 2.2 `ParameterTrack<MAX_SIZE>`

Each parameter track is aliased from `rpdsp::ParameterTrack<float, MAX_SIZE>` (`src/rpdsp/src/rpdsp/parameter_track.h`):

```cpp
template <uint8_t MAX_SIZE>
using ParameterTrack = rpdsp::ParameterTrack<float, MAX_SIZE>;
```

Key characteristics:
- **Maximum Step Capacity**: `SequencerConstants::MAX_STEPS_COUNT = 64` steps.
- **Independent Length**: Configured via `resize(newStepCount)` or `ParameterManager::setStepCount()`. Clamped to `[1, 64]`.
- **Modulo Indexing**: Calling `getValue(stepIdx)` computes `stepIdx % currentStepCount`, automatically wrapping around the track's individual step count.
- **Static Storage**: Fixed-size internal buffer avoids dynamic allocation during runtime.

### 2.3 Parameter Metadata (`CORE_PARAMETERS`)

Metadata and defaults for all parameters are defined in `CORE_PARAMETERS` (`SequencerDefs.h`):

```cpp
struct ParameterDefinition
{
  const char *name;                // Display name
  ParameterValueType defaultValue; // std::variant<int, float, bool>
  ParameterValueType minValue;     // Minimum valid value
  ParameterValueType maxValue;     // Maximum valid value
  bool isBinary;                   // True for gate/slide
  uint8_t defaultSteps;            // Default step count (16)
};

constexpr ParameterDefinition CORE_PARAMETERS[] = {
  // Name          Default  Min    Max    Binary  Default Steps
  {"Note",         0.0f,    0.0f,  21.0f, false,  16},
  {"Velocity",     0.5f,    0.0f,  1.0f,  false,  16},
  {"Filter",       0.5f,    0.0f,  1.0f,  false,  16},
  {"Attack",       0.01f,   0.0f,  1.0f,  false,  16},
  {"Decay",        0.3f,    0.0f,  1.0f,  false,  16},
  {"Octave",       0.0f,    0.0f,  1.0f,  false,  16},
  {"GateLength",   0.3f,    0.001f,1.0f,  false,  16},
  {"Gate",         false,   false, true,  true,   16},
  {"Slide",        false,   false, true,  true,   16}
};
```

### 2.4 `ParameterManager` Class

`ParameterManager` owns and coordinates the 9 parameter tracks for a single sequencer instance:

```cpp
class ParameterManager
{
public:
    void init();
    void setStepCount(ParamId id, uint8_t steps);
    uint8_t getStepCount(ParamId id) const;
    float getValue(ParamId id, uint8_t stepIdx) const;
    void setValue(ParamId id, uint8_t stepIdx, float value);
    void randomizeParameters();

private:
    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
};
```

- **Clamping and Rounding in `setValue`**:
  `setValue()` clamps the incoming value between `CORE_PARAMETERS[id].minValue` and `maxValue`. If `isBinary` is true, it thresholds at `> 0.5f` to produce `0.0f` or `1.0f`. If `minValue` is an integer variant, it rounds using `roundf()`.
- **Randomization Algorithm (`randomizeParameters`)**:
  Uses an internal Linear Congruential Generator (LCG) seeded from system time. Applies musical heuristics per parameter:
  - `Gate`: Even steps have a 75% probability of being active (1/4 chance of 0); odd steps have a ~33% probability (1/3 chance of 1).
  - `Slide`: 1/13 chance (~7.7%) per step; track is always resized to 64 steps for safety.
  - `Attack` / `Decay`: Weighted towards short attacks and medium decays with occasional long swells.
  - `Filter`: Uniform random in range `[0.2, 0.95]`.

---

## 3. Sequencer Public API & Core Advancement

### 3.1 Class Declaration (`Sequencer.h`)

```cpp
class Sequencer
{
public:
    Sequencer();
    Sequencer(uint8_t channel); // channel: 1..4 corresponding to voices
    ~Sequencer() = default;

    // Initialization & Reset
    void initializeParameters();
    void resetAllSteps();
    void reset();

    // Step Parameter Access
    float getStepParameterValue(ParamId id, uint8_t stepIdx) const;
    void setStepParameterValue(ParamId id, uint8_t stepIdx, float value);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);
    uint8_t getCurrentStep() const;
    uint8_t getCurrentStepForParameter(ParamId paramId) const;
    Step getStep(uint8_t stepIdx) const;

    // Step Execution & Preview
    void playStepNow(uint8_t stepIdx, VoiceState *voiceState);
    void toggleStep(uint8_t stepIdx);

    // Transport Control
    void start() { running = true; }
    void stop() { running = false; }
    bool isRunning() const { return running; }
    void randomizeParameters();

    // Note & Envelope Timing
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState *voiceState);
    void tickNoteDuration(VoiceState *voiceState);
    bool isNotePlaying() const;
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    // Core Step Advancement (Primitive Signature)
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                     bool is_note_button_held, bool is_velocity_button_held,
                     bool is_filter_button_held, bool is_attack_button_held,
                     bool is_decay_button_held, bool is_octave_button_held,
                     int current_selected_step_for_edit,
                     VoiceState *voiceState);
};
```

> **Important**: `Sequencer` does **not** include or accept `UIState`. All UI parameters are unpacked before calling `Sequencer::advanceStep()`.

### 3.2 `Sequencer::advanceStep` Implementation Flow

When `advanceStep()` is called on each 16th note clock tick:

1. **Check Running State**: If `!running`, returns immediately without updating state.
2. **Sequence Length Calculation**:
   Calculates `currentStep = current_uclock_step % getParameterStepCount(ParamId::Gate)`.
4. **Independent Parameter Stepping**:
   For each parameter track `i` in `0..ParamId::Count-1`:
   ```cpp
   currentStepPerParam[i] = current_uclock_step % getParameterStepCount(paramId);
   ```
5. **Real-time Parameter Recording**:
   If `mm_distance >= 0` and not in step-edit mode (`current_selected_step_for_edit == -1`):
   - Normalizes distance: `normalized = clamp(mm_distance / 1400.0f, 0.0f, 1.0f)`.
   - Checks held parameter buttons. For `ParamId::Note`, checks gate restriction (recording is allowed only if the step's Gate is HIGH).
   - Maps normalized distance to the target parameter's range and calls `setStepParameterValue(paramId, currentStepPerParam[paramId], value)`.
6. **Step Processing (`processStep`)**:
   Calls `processStep(UINT8_MAX, voiceState)` to populate the output `VoiceState`:
   - Extracts all parameter values at their respective `currentStepPerParam[id]` indices.
   - Converts octave parameter via `mapFloatToOctaveOffset()`:
     - `octave < 0.15f` &rarr; `-12` semitones
     - `octave > 0.40f` &rarr; `+12` semitones
     - `0.15f <= octave <= 0.40f` &rarr; `0` semitones
   - Slide Handling: If `hasSlide` is true, envelope is not retriggered (`voiceState->shouldRetrigger = false`); note frequency transitions smoothly via slewing in `Voice`.
   - Gate-Controlled Note Output: If Gate is LOW, previous `noteIndex` and `octaveOffset` are retained in `VoiceState`, allowing sustaining/releasing notes to fade out naturally without glitching.

---

## 4. UI Adapter & Firmware Integration

### 4.1 Adapter Function (`advanceSequencerStep`)

The bridge between `UIState` and `Sequencer` is declared in `src/ui/UIEventHandler.h` and implemented in `src/ui/UIEventHandler.cpp`:

```cpp
void advanceSequencerStep(Sequencer &seq, uint8_t current_uclock_step, int mm_distance,
                          const UIState &uiState, VoiceState *voiceState)
{
  seq.advanceStep(current_uclock_step, mm_distance,
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Note)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Velocity)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Filter)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Attack)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Decay)],
                  uiState.parameterButtonHeld[static_cast<int>(ParamId::Octave)],
                  uiState.selectedStepForEdit,
                  voiceState);
}
```

### 4.2 Main Step Callback (`onStepCallback` / `processSequencerStep` in `Pico2Seq.ino`)

`uClock` invokes `onStepCallback()` on Core 0 (timer ISR context) on every 16th note, but it only enqueues the step number into the 16-deep `stepQueue` SPSC ring. `loop()` drains the queue via `processClockEvents()` → `processSequencerStep()` in thread context, which does the full step work:

```cpp
// ISR context — stage only
void onStepCallback(uint32_t uClockCurrentStep)
{
    if (!stepQueue.tryPush(uClockCurrentStep))
    {
        droppedStepCount++; // loop() stalled longer than the queue
    }
}

// Thread context — drained by processClockEvents() in loop()
void processSequencerStep(uint32_t uClockCurrentStep)
{
    currentSequencerStep = static_cast<uint8_t>(uClockCurrentStep);

    VoiceState tempState1, tempState2, tempState3, tempState4;

    // Route distance sensor to the currently selected voice (0..3); others disabled (-1)
    int v1Distance = (uiState.selectedVoiceIndex == 0) ? mm : -1;
    int v2Distance = (uiState.selectedVoiceIndex == 1) ? mm : -1;
    int v3Distance = (uiState.selectedVoiceIndex == 2) ? mm : -1;
    int v4Distance = (uiState.selectedVoiceIndex == 3) ? mm : -1;

    // 1. Advance all 4 sequencers
    advanceSequencerStep(seq1, uClockCurrentStep, v1Distance, uiState, &tempState1);
    advanceSequencerStep(seq2, uClockCurrentStep, v2Distance, uiState, &tempState2);
    advanceSequencerStep(seq3, uClockCurrentStep, v3Distance, uiState, &tempState3);
    advanceSequencerStep(seq4, uClockCurrentStep, v4Distance, uiState, &tempState4);

    VoiceState tempStates[] = {tempState1, tempState2, tempState3, tempState4};

    // 2. Apply encoder base values per voice (all 4 voices) and global delay values
    for (uint8_t voiceIndex = 0; voiceIndex < VoiceSystem::MAX_VOICES; voiceIndex++)
    {
        applyEncoderBaseValues(&tempStates[voiceIndex], voiceIndex);
    }
#if PICO2SEQ_ENABLE_DELAY_EFFECT
    applyEncoderDelayValues();
#endif

    // 3. Update VoiceSystem and MIDI hardware
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        if (i < 2) // Voices 0 and 1: gate timers & MIDI note lifecycle
        {
            updateVoiceMIDI(tempStates[i], i, true,
                            &voiceSystem.getGate(i),
                            &voiceSystem.getGateTimer(i));
        }
        else // Voices 2 and 3: audio synthesis only
        {
            updateVoiceMIDI(tempStates[i], i, false);
        }

        voiceSystem.getVoiceState(i) = tempStates[i];
    }
}
```

---

## 5. Timing, Groove & Shuffle System

### 5.1 uClock & PPQN Timing Architecture

- **PPQN Standard**: 480 Pulses Per Quarter Note (`SequencerConstants::PULSES_PER_QUARTER_NOTE_PPQN = 480`).
- **Ticks Per 16th Note Step**: `PULSES_PER_SEQUENCER_STEP_TICKS = 480 / 4 = 120` clock ticks.
- **Default Tempo**: 90 BPM (configured during `uClock.init()` in `setup()`).

### 5.2 `ShuffleTemplates.h` Groove Templates

The shuffle engine uses 16-step offset templates defined in `src/pico2seq-core/sequencer/ShuffleTemplates.h`:

```cpp
const int NUM_SHUFFLE_TEMPLATES = 16;
const int SHUFFLE_TEMPLATE_SIZE = 16;

struct ShuffleTemplate
{
    const char *name;
    int8_t ticks[SHUFFLE_TEMPLATE_SIZE];
};
```

Each entry in `ticks[16]` specifies a clock tick offset for that 16th note step:
- **Positive value (`> 0`)**: Delays the step (swings late).
- **Negative value (`< 0`)**: Advances the step (pushes early).
- **Zero (`0`)**: Exact on-grid timing.

#### Complete List of 16 Shuffle Templates

| Index | Template Name | Characteristic Tick Offsets | Musical Feel / Description |
|---|---|---|---|
| 0 | `"No Shuffle"` | `0, 0, 0, 0, ...` | Strict straight 16th grid |
| 1 | `"Teeny Swing"` | `0, 5, 0, 6, 0, 5, ...` | Subtle humanized micro-swing |
| 2 | `"Lil' Swing (53%)"` | `0, 10, 0, 11, 0, 10, ...` | Classic 53% light groove |
| 3 | `"Neg' Swing (53%)"` | `0, -10, 0, -11, 0, -10, ...` | Pushed / rushed upbeat feel |
| 4 | `"CornBread"` | `0, 13, 0, 14, 0, 13, 0, 15...` | Asymmetric organic Southern groove |
| 5 | `"Swing (55%)"` | `0, 17, 0, 18, ...` | Medium standard swing |
| 6 | `"Swing (56%)"` | `0, 19, 0, 18, 0, 19, 0, 20...` | Moderate jazz / house swing |
| 7 | `"Swing (57%)"` | `0, 23, 0, 22, ...` | Pronounced dance swing |
| 8 | `"Swing (60%)"` | `0, 30, 0, 28, 0, 31, ...` | Triplet-feel swing (60%) |
| 9 | `"Big Swang (60%)"` | `0, 36, 0, 34, 0, 36, ...` | Heavy laid-back swing |
| 10 | `"Phatty Swang"` | `0, 40, 0, 40, ...` | Deep MPC-style swing |
| 11 | `"Big Swang (62%)"` | `0, 50, 0, 48, ...` | Extreme hard swing |
| 12 | `"Humanize 1"` | `0, -1, 3, 1, 0, -2, 1...` | Micro-timing drummer variations |
| 13 | `"Humanize 2"` | `0, -1, 0, 2, 1, 2, ...` | Loose unquantized live feel |
| 14 | `"Hip-Hop"` | `0, 40, 0, 22, 0, 40, 0, 22...` | Boom-bap asymmetric late snare swing |
| 15 | `"Funk Groove"` | `0, 35, 0, 20, 0, 30, 0, 20...` | Syncopated funk pocket timing |

### 5.3 Applying Shuffle in Firmware

When cycling swing patterns via the control surface (Button 29 / `BUTTON_CHANGE_SWING_PATTERN` in `src/ui/ButtonHandlers.cpp`):

```cpp
case BUTTON_CHANGE_SWING_PATTERN:
{
    state.currentShufflePatternIndex = (state.currentShufflePatternIndex + 1) % NUM_SHUFFLE_TEMPLATES;
    const ShuffleTemplate &currentTemplate = shuffleTemplates[state.currentShufflePatternIndex];
    
    // Apply template buffer to uClock
    uClock.setShuffleTemplate(const_cast<int8_t *>(currentTemplate.ticks), SHUFFLE_TEMPLATE_SIZE);
    uClock.setShuffle(state.currentShufflePatternIndex > 0); // Enable shuffle if not "No Shuffle"
}
break;
```

The OLED display retrieves human-readable names via `getShuffleTemplateName(index)`:

```cpp
inline const char *getShuffleTemplateName(uint8_t index)
{
    if (index >= NUM_SHUFFLE_TEMPLATES) return "Invalid";
    return shuffleTemplates[index].name;
}
```

---

## 6. Hardware Gate Outputs (Removed)

Earlier revisions of `Sequencer.cpp` drove hardware gate/clock pins directly
(GPIO 10 = Voice 1 gate, GPIO 11 = Voice 2 gate, GPIO 12 = step-clock pulse)
for external modular sync. Those GPIO outputs have been **removed**: the pins
are now used by the PIO I2S audio output (BCLK/LRCK/DATA), and the core no
longer contains any Arduino/GPIO calls — `src/pico2seq-core/` is fully
portable again. Voice on/off timing lives entirely in the software `Gate`
parameter track, `VoiceState::isGateHigh`, and the VoiceSystem gate timers.

---

## 7. Gate-Controlled Note Programming & Slide Operation

### 7.1 Gate-Controlled Note Editing

To prevent accidental modification of pitch parameters on inactive steps during live performance or parameter recording:

1. **Step Parameter Assignment (`setStepParameterValue`)**:
   ```cpp
   if (id == ParamId::Note)
   {
       float gateValue = getStepParameterValue(ParamId::Gate, stepIdx);
       if (gateValue <= 0.5f) {
           return; // Silently ignore note edits on inactive steps
       }
   }
   ```
2. **Real-time Sensor Recording**:
   During live distance-sensor parameter recording, if `ParamId::Note` is selected, `Sequencer::advanceStep` skips updating steps whose Gate is currently `0.0f`.

### 7.2 Slide / Portamento Logic

When a step has `hasSlide = true`:
1. `voiceState->shouldRetrigger = false`: The ADSR envelope is **not** retriggered, allowing the note to sustain continuously.
2. `noteDuration.start(noteDurationTicks)`: The note duration timer is refreshed for the new step.
3. `previousStepHadSlide`: If a gate-off step immediately follows a slide step, `handleNoteOff()` is bypassed so the sliding note can ring out smoothly.
4. Pitch slewing is performed inside `Voice::processFrequencySlew()` using exponential filter coefficient `slideAlpha = 1.0f - std::exp(-1.0f / (slideTimeSeconds * sampleRate))`.

---

## 8. Summary of Data Structures & State Containers

| Struct / Class | Location | Primary Purpose |
|---|---|---|
| `Sequencer` | `src/pico2seq-core/sequencer/Sequencer.h/.cpp` | Core step sequencer logic, parameter automation, note lifecycle |
| `ParameterManager` | `src/pico2seq-core/sequencer/ParameterManager.h/.cpp` | 9 independent `ParameterTrack<64>` instances, value clamping, randomization |
| `ParameterTrack<64>` | `src/rpdsp/src/rpdsp/parameter_track.h` | Fixed-size polymetric track with modulo wrapping |
| `VoiceState` | `src/pico2seq-core/sequencer/SequencerDefs.h` | Parameter container emitted per step to configure `Voice` DSP and MIDI |
| `Step` | `src/pico2seq-core/sequencer/SequencerDefs.h` | Internal parameter snapshot for step editing and inspection |
| `GateTimer` | `src/pico2seq-core/sequencer/SequencerDefs.h` | Volatile tick-countdown timer for automatic note-off handling |
| `ShuffleTemplate` | `src/pico2seq-core/sequencer/ShuffleTemplates.h` | 16-step microtiming offsets for 480 PPQN groove templates |
| `advanceSequencerStep` | `src/ui/UIEventHandler.h/.cpp` | Firmware UI adapter bridging `UIState` to `Sequencer::advanceStep` |