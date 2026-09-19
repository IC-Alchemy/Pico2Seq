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
                              │       VoiceManager::processBlock() @ 48kHz         │
                              │           Voice DSP (source -> VCA -> filter)         │
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
   - **Core 1** renders audio in blocks with per-sample DSP state updates in `VoiceManager::processBlock()` using the parameter values generated by Core 0.
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
  Note,       // 0 - Scale step index (0-36, maps to SCALE_STEPS array)
  Velocity,   // 1 - Voice amplitude level (0.0-1.0)
  Filter,     // 2 - Filter cutoff frequency (0.0-1.0)
  Attack,     // 3 - Envelope attack time (0.0-1.0 seconds)
  Decay,      // 4 - Envelope decay time (0.0-1.0 seconds)
  Octave,     // 5 - Normalized octave control, mapped to -24/-12/0/+12/+24 semitones (-2..+2 oct)
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
  const char *name;                // Display name for UI
  ParameterValueType defaultValue; // std::variant<int, float, bool>
  ParameterValueType minValue;     // Minimum valid value
  ParameterValueType maxValue;     // Maximum valid value
  ParameterEditKind editKind;      // Continuous, Stepped, or Toggle
  uint8_t defaultSteps;            // Default step count (16)
  bool recordable;                 // Has a parameter-button live-record control;
                                   // not a restriction on explicit step/fader edits
  EncoderParameterMode encoderMode; // COUNT when there is no encoder base target
};

constexpr ParameterDefinition CORE_PARAMETERS[] = {
  // Name, default, min, max, edit kind, steps, recordable, encoder base target
  {"Note",       0,     0,     36,   Stepped,    16, true,  EncoderParameterMode::Note},
  {"Velocity",   0.5f,  0.0f,  1.0f, Continuous, 16, true,  EncoderParameterMode::Velocity},
  {"Filter",     0.5f,  0.0f,  1.0f, Continuous, 16, true,  EncoderParameterMode::Filter},
  {"Attack",     0.01f, 0.0f,  1.0f, Continuous, 16, true,  EncoderParameterMode::Attack},
  {"Decay",      0.3f,  0.0f,  1.0f, Continuous, 16, true,  EncoderParameterMode::Decay},
  {"Octave",     0.5f,  0.0f,  1.0f, Stepped,    16, true,  EncoderParameterMode::Octave},
  {"GateLength", 0.5f,  0.001f,1.0f, Continuous, 16, false, EncoderParameterMode::COUNT},
  {"Gate",       false, false, true, Toggle,     16, false, EncoderParameterMode::COUNT},
  {"Slide",      false, false, true, Toggle,     16, false, EncoderParameterMode::COUNT}
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
    void copyStep(uint8_t srcStep, uint8_t dstStep);
    // Direct (non-wrapping) access for persistence.
    float getRawValue(ParamId id, uint8_t stepIdx) const;
    void setRawValue(ParamId id, uint8_t stepIdx, float value);

    static constexpr uint8_t kDefaultRandomizeDepth = 35;
    void randomizeParameters(uint8_t depthPercent = kDefaultRandomizeDepth, uint64_t seed = 0);
    void setLaneAmount(ParamId id, uint8_t percent); // clamps to 0-100
    uint8_t getLaneAmount(ParamId id) const;

private:
    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
    LaneAmounts _laneAmounts; // 0-100 percent per lane
};
```

- **Clamping and Rounding in `setValue`**:
  `setValue()` clamps the incoming value between `CORE_PARAMETERS[id].minValue` and `maxValue`. If the lane's `editKind` is `Toggle`, it thresholds at `> 0.5f` to produce `0.0f` or `1.0f`. If `minValue` is an integer variant, it rounds using `roundf()`. `setValue` remains the storage-domain validator — all writes (including `Sequencer::writeStepParameter`, below) land through it.
- **Randomization Algorithm (`randomizeParameters`)**:
  Uses an internal Linear Congruential Generator; `seed == 0` seeds from the clock, any other seed repeats deterministically. The rhythm stays the player's: `Gate` and `Slide` are never rewritten. Per lane:
  - `Note`: draws scale steps `0-12` (playback quantizes them to the scale and adds the base transpose).
  - `Octave` / `GateLength`: return to their neutral midpoint.
  - `Velocity` / `Filter` / `Attack` / `Decay`: triangular offsets around the neutral modifier `0.5` (the sum of two uniform draws), scaled by each lane's amount — most steps stay near the preset base, a few reach the depth's edge. A lane amount of `0` (see `setLaneAmount`) leaves that lane untouched.

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
    StepWriteResult writeStepParameter(ParamId id, uint8_t stepIdx, float value,
                                       StepWriteDomain domain, bool recording,
                                       NoteGateRule noteGate = NoteGateRule::None);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);
    uint8_t getCurrentStep() const;
    int8_t getCurrentNote() const;
    uint8_t getCurrentStepForParameter(ParamId paramId) const;
    Step getStep(uint8_t stepIdx) const;
    void setStep(uint8_t stepIdx, const Step &step);
    void copyStep(uint8_t srcStep, uint8_t dstStep);
    Step getPlaybackStep(uint8_t stepIdx = UINT8_MAX) const;

    // Step Execution & Preview
    void playStepNow(uint8_t stepIdx, VoiceState *voiceState);
    void previewActiveStep(VoiceState *voiceState);
    void refreshVoiceParameters(VoiceState *voiceState) const;
    void refreshVoiceParametersAt(uint8_t stepIdx, VoiceState *voiceState) const;
    void toggleStep(uint8_t stepIdx);

    // Transport Control
    void start() { running = true; }
    void stop() { running = false; }
    bool isRunning() const { return running; }
    void randomizeParameters(uint8_t depthPercent = ParameterManager::kDefaultRandomizeDepth,
                             uint64_t seed = 0);

    // Note & Envelope Timing
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState *voiceState);
    bool tickNoteDuration(VoiceState *voiceState);
    bool isNotePlaying() const;
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    // Core Step Advancement (Primitive Signature)
    void advanceStep(uint32_t current_uclock_step, int mm_distance,
                     bool is_note_button_held, bool is_velocity_button_held,
                     bool is_filter_button_held, bool is_attack_button_held,
                     bool is_decay_button_held, bool is_octave_button_held,
                     int current_selected_step_for_edit,
                     VoiceState *voiceState);
};
```

> **Important**: `Sequencer` does **not** include or accept `UIState`. All UI parameters are unpacked before calling `Sequencer::advanceStep()`.

> **Voice Editing playback transform (2026-09-11)**: `Sequencer` also carries a
> `setPlaybackTransform()` callback (with an optional octave mapper). It is installed only
> during playback so sequenced lanes compose with each voice's edit bases (base + modifier, with Note as melody + scale-step transpose)
> without ever rewriting stored steps — see `Sequencer.h` and
> [`docs/voice-edit.md`](voice-edit.md).

### 3.2 Shared Step Write Operation (`writeStepParameter`)

Every lane writer routes through one shared stored-lane write (`Sequencer.h`/`Sequencer.cpp`), so validation, input normalization, Note gate protection, and change detection exist exactly once:

```cpp
StepWriteResult writeStepParameter(ParamId id, uint8_t stepIdx, float value,
                                   StepWriteDomain domain, bool recording,
                                   NoteGateRule noteGate = NoteGateRule::None);

struct StepWriteResult
{
    StepWriteStatus status; // Rejected | AcceptedUnchanged | Changed
    float previousStored;
    float stored;           // valid for AcceptedUnchanged and Changed
};
```

- **`StepWriteDomain`**: `Normalized01` (sensor/fader position in 0..1, scaled through `mapNormalizedValueToParamRange`) or `LaneValue` (already in the lane's stored units — encoder step edits).
- **`NoteGateRule`** (consulted only for recording writes to `ParamId::Note`): `None` (explicit editing — rest steps stay writable), `AtGateCursor` (live recording — the Gate lane's own cursor value), `AtStep` (selected-step recording — the stored Gate at that step).
- **Validation**: rejects non-recordable lanes (`Gate`, `Slide`, `GateLength` keep their dedicated interaction rules), out-of-range steps, and non-finite values.
- **Change detection**: the read-back comparison happens **after** storage-domain rounding, so callers never republish quantization no-ops — `AcceptedUnchanged` writes do not refresh or retransmit the voice.
- **The four lane writers** (all host-tested in `tests/unit/test_step_write.cpp` / `test_edit_publication.cpp`):
  1. **Live clock recording** — `advanceStep()` (below); each armed lane records at its own cursor, Note against `AtGateCursor`.
  2. **Selected/stopped lidar recording** — `updateParametersForStepNormalized()` in `src/app/StepPlayback.cpp`; Note against the stored `AtStep` gate, and only a `Changed` result refreshes the sounding voice via `updateActiveVoiceState()`.
  3. **Selected-step encoder edits** — `editSelectedStep()` in `src/sensors/EncoderManager.cpp`; explicit (`recording = false`, `NoteGateRule::None`), so rest steps are writable.
  4. **Fader step edits** — `AlchemyControlBridge::handleFaders()` in `src/ui/AlchemyControlBridge.cpp`; recording semantics plus change detection suppress redundant publishes.

`ParameterManager::setValue` remains the storage-domain validator and `VoiceEdit::composeLane` remains the playback composition — neither is duplicated by the write op.

**Publication**: accepted-but-unchanged writes no longer republish. When a stopped-time edit changes a step, `updateActiveVoiceState()` (`src/app/StepPlayback.cpp`) refreshes the voice through `Sequencer::refreshVoiceParametersAt(step, state)` — the stopped-preview consolidation of the previous per-site `Step` → `VoiceState` assignments (no retrigger, pitch unconditional). While the clock runs, `refreshVoiceParameters()` (cursor mode, pitch only while gated) is used instead.

**Editing contract, base vs. step**: the encoder edits the voice's **patch base** (`VoiceConfig`, via `VoiceEditor::encoder()`) except when a step is selected for editing, in which case the turn goes to that step's stored lane value via `editSelectedStep()`. A manual edit (encoder turn or accepted fader move — including one pinned at a lane limit) takes **ownership** of its voice+lane+step target (`ControlSurface::StepEditOwnership` in `src/ui/ControlSurfaceLogic.h`, held in `UIState::stepEditOwner`): lidar writes to that exact target are suppressed until the hand leaves the sensor window and returns, and ownership resets when the voice, selected step, focused parameter, mode, or modal editor changes. Encoder-vs-fader in one pass is deterministic: the later (encoder) write wins.

### 3.3 `Sequencer::advanceStep` Implementation Flow

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
   - Normalizes distance: `normalized = clamp(mm_distance / 1100.0f, 0.0f, 1.0f)`.
   - Checks the held parameter buttons (the full armed set, so several lanes can record at once). Each held lane records at **its own** track cursor via the shared write: `writeStepParameter(paramId, currentStepPerParam[paramId], normalized, StepWriteDomain::Normalized01, true, NoteGateRule::AtGateCursor)`.
   - For `ParamId::Note`, the `AtGateCursor` rule refuses the write when the Gate lane's own cursor value is LOW, so muted steps never take pitch data.
6. **Step Processing (`processStep`)**:
   Calls `processStep(UINT8_MAX, voiceState)` to populate the output `VoiceState`:
   - Extracts all parameter values at their respective `currentStepPerParam[id]` indices.
    - Calculates final note value and clamps to valid MIDI range `[0, 127]`.
    - Converts octave parameter via `VoiceEdit::mapOctave()` (or injected `octaveMapper_`):
      - Stored `0.00` &rarr; `-24` semitones (-2 oct)
      - Stored `0.25` &rarr; `-12` semitones (-1 oct)
      - Stored `0.50` &rarr; `0` semitones (0 oct)
      - Stored `0.75` &rarr; `+12` semitones (+1 oct)
      - Stored `1.00` &rarr; `+24` semitones (+2 oct)
    - Slide Handling: If `!slideVal || !noteActive`, envelope retriggers (`voiceState->shouldRetrigger = true`). If sliding from an already active note (`slideVal && noteActive`), `shouldRetrigger = false` and note frequency transitions smoothly via slewing in `Voice`.
   - Gate-Controlled Note Output: If Gate is LOW, previous `noteIndex` and `octaveOffset` are retained in `VoiceState`, allowing sustaining/releasing notes to fade out naturally without glitching.

---

## 4. UI Adapter & Firmware Integration

### 4.1 Adapter Function (`advanceSequencerStep`)

The bridge between `UIState` and `Sequencer` is declared in `src/ui/UIEventHandler.h` and implemented in `src/ui/UIEventHandler.cpp`:

```cpp
void advanceSequencerStep(Sequencer &seq, uint32_t current_uclock_step, int mm_distance,
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

### 4.2 Main Step Callback (`onStepCallback` / `processSequencerStep` in `src/app/`)

`uClock` invokes `onStepCallback()` on Core 0 (timer ISR context) on every 16th note, but it only enqueues the step number into the 16-deep SPSC ring (`src/app/ClockService.cpp`). The `loop()` in `src/app/Application.cpp` drains the queue via `processClockEvents()` → `processSequencerStep()` (in `src/app/StepPlayback.cpp`) in thread context, which does the full step work:

```cpp
// ISR context — stage only (src/app/ClockService.cpp)
void onStepCallback(uint32_t uClockCurrentStep)
{
    if (!clockEvents.steps.tryPush(uClockCurrentStep))
    {
        clockEvents.droppedSteps++; // loop() stalled longer than the queue
    }
}

// Thread context — StepPlayback.cpp, called from processClockEvents()
void processSequencerStep(uint32_t uClockCurrentStep)
{
    if(!isClockRunning || uiState.voiceEditor.active) return;

    VoiceState tempStates[VoiceSystem::MAX_VOICES];
    const uint8_t selectedVoice = uiState.selectedVoiceIndex;
    // With no hand in range, live recording pauses and steps keep their values.
    const int handDistance = AppState::performanceInput.handPresent
                                 ? AppState::performanceInput.distanceAboveMinimumMm : kDistanceDisabled;
    // First advance all four voices. Only the selected voice hears the sensor.
    for (uint8_t voice = 0; voice < VoiceSystem::MAX_VOICES; ++voice)
    {
        const int distance = voice == selectedVoice ? handDistance : kDistanceDisabled;
        AppState::sequencers[voice]->setRecordingInput(AppState::performanceInput.recordingValue());
        advanceSequencerStep(*AppState::sequencers[voice], uClockCurrentStep,
                             distance, uiState, &tempStates[voice]);
    }

    // Bases have already been composed by each sequencer's playback transform.
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
        publishVoiceState(i, tempStates[i]);
}
```

**The encoder is not part of the step callback.** Patch-base editing is handled in the 1 ms control pass (`ControlIO::scanControls()` in `src/app/ControlIO.cpp`) via `updateEncoderBaseValues()` (`src/sensors/EncoderManager.cpp`), which forwards each read's increment either to `editSelectedStep()` (a step is selected: the turn writes that step's stored lane through the shared write op, §3.2) or to `VoiceEditor::encoder()` (base editing of the encoder target's `VoiceConfig` lane, composed during playback by each sequencer's playback transform). There is no per-step `applyEncoderBaseValues()` pass anymore; the old `EncoderBaseValues` offset structs were removed when patch bases moved into `VoiceConfig`.

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

To prevent accidental modification of pitch parameters on inactive steps during live performance or parameter recording, gate protection lives in the shared write op (`Sequencer::writeStepParameter`, §3.2) and applies to **recording** writes only:

- **Live recording** (`NoteGateRule::AtGateCursor`): `Sequencer::advanceStep` refuses a Note write when the Gate lane's own cursor value is `<= 0.5f`.
- **Selected/stopped recording** (`NoteGateRule::AtStep`): `updateParametersForStepNormalized()` (`src/app/StepPlayback.cpp`) and the fader step path (`src/ui/AlchemyControlBridge.cpp`) refuse a Note write when the **stored Gate at the edited step** is `<= 0.5f`.
- **Explicit editing** (`recording = false`, e.g. `editSelectedStep()`): no gate rule — rest steps are writable, so a step can be given a pitch before its gate is armed.

A gate-rejected write returns `StepWriteStatus::Rejected` with the stored value retained, and the OLED keeps showing that retained value (values come from storage, never from a sensor prediction — see `src/ui/OledView.h`).

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