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
                              │   onStepCallback() (stage into clockEvents.steps)      │
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
                              │             (adapter in src/app/)                      │
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
                              │          (Voice IDs, Control Snapshots)                │
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
   Because `Sequencer::advanceStep()` accepts only portable types (integers, floats, and the plain `StepEditButtons` aggregate), the firmware bridges the rich `UIState` struct via the private adapter function `advanceSequencerStep()` in `src/app/StepPlayback.cpp`.
3. **Polymetric Parameter Tracks**:
   Rather than advancing all synthesis parameters in lockstep, every parameter (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide, Sustain, Release) operates on an independent `ParameterTrack<64>` with its own step count (2–64 steps). This allows patterns such as a 16-step melody, an 8-step filter pattern, and a 5-step velocity cycle to run simultaneously on a single voice.
4. **Dual-Core Execution**:
   - **Core 0** runs `uClock` (timer ISR on core 0 — stock library). The ISR-stage-only `onStepCallback()` queues each 16th note; `loop()` drains it (`processClockEvents()` → `processSequencerStep()`), advancing all 4 sequencers and publishing voice states in thread context. PPQN drains advance sequencer-owned note durations and publish gate-off on expiry.
   - **Core 1** renders audio in blocks with per-sample DSP state updates in `VoiceManager::processBlock()` using the parameter values generated by Core 0.
5. **4-Voice System Integration**:
   - All four voices use the same sequencer-owned note lifecycle and audio publication path.
   - Optional portable MIDI callbacks remain for other applications; the firmware has no MIDI transport or separate MIDI tracker.

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
  Sustain,    // 9 - Envelope sustain level (0.0-1.0), added 2026-09-19
  Release,    // 10 - Envelope release time (0.0-1.0 normalized), added 2026-09-19
  Count       // 11 - Total parameter count for array sizing
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
  const char *name;                 // Display name for UI
  ParameterValueType defaultValue;  // std::variant<int, float, bool>
  ParameterValueType minValue;      // Minimum allowed value
  ParameterValueType maxValue;      // Maximum allowed value
  ParameterEditKind editKind;       // Continuous, Stepped (detented), or Toggle (gate/slide)
  uint8_t defaultSteps;             // Default step count (16)
  bool recordable;                  // Has a parameter-button live-record control
  EncoderParameterMode encoderMode; // COUNT when the lane has no encoder base target
  bool patchDefault;                // Absolute lane: a step may follow the patch value
};
```

`CORE_PARAMETERS` (`SequencerDefs.h`, ordered like `ParamId`) holds one row per lane; every lane defaults to 16 steps:

| Lane | Default | Min | Max | Edit kind | Recordable | Encoder base | `patchDefault` |
|---|---|---|---|---|---|---|---|
| Note | 0 | 0 | 36 | Stepped | yes | Note | no |
| Velocity | 0.5 | 0.0 | 1.0 | Continuous | yes | Velocity | yes |
| Filter | 0.5 | 0.0 | 1.0 | Continuous | yes | Filter | yes |
| Attack | 0.01 | 0.0 | 1.0 | Continuous | yes | Attack | yes |
| Decay | 0.3 | 0.1 | 1.0 | Continuous | no | none | yes |
| Octave | 0.5 | 0.0 | 1.0 | Stepped | yes | Octave | no |
| GateLength | 0.8 | 0.1 | 1.0 | Continuous | no | none | no |
| Gate | false | false | true | Toggle | no | none | no |
| Slide | false | false | true | Toggle | no | none | no |
| Sustain | 0.5 | 0.0 | 1.0 | Continuous | no | none | yes |
| Release | 0.3 | 0.01 | 1.0 | Continuous | yes | Release | yes |

`recordable` only says whether a lane has a parameter-button live-record control; explicit step or fader edits are not restricted by it. Lanes with `patchDefault` (Velocity,
Filter, Attack, Decay, Sustain, Release) are **absolute**: a step stores its own
0–1 value, or `SequencerConstants::LANE_FOLLOWS_PATCH` (-1) to play the voice's
patch value. `ParameterManager::setValue()` stores that sentinel unclamped;
`Sequencer::followPatch()` writes it, `patchValue()` reads what it plays.

### 2.4 `ParameterManager` Class

`ParameterManager` owns and coordinates the 11 parameter tracks for a single sequencer instance:

```cpp
class ParameterManager
{
public:
    void init();
    void fillTrack(ParamId id, float value);   // all 64 slots, length unchanged
    void setStepCount(ParamId id, uint8_t steps);
    uint8_t getStepCount(ParamId id) const;
    float getValue(ParamId id, uint8_t stepIdx) const;
    void setValue(ParamId id, uint8_t stepIdx, float value);
    void copyStep(uint8_t srcStep, uint8_t dstStep);
    float getRawValue(ParamId id, uint8_t stepIdx) const;        // save/load: no clamp/round
    void setRawValue(ParamId id, uint8_t stepIdx, float value);
    void randomizeParameters(uint8_t depthPercent = 35, uint64_t seed = 0);
    void setLaneAmount(ParamId id, uint8_t percent);  // per-lane humanize depth, 0-100
    uint8_t getLaneAmount(ParamId id) const;

private:
    ParameterTrack<SequencerConstants::MAX_STEPS_COUNT> _tracks[static_cast<size_t>(ParamId::Count)];
};
```

- **Clamping and Rounding in `setValue`**:
  `setValue()` clamps the incoming value between `CORE_PARAMETERS[id].minValue` and `maxValue`. If the lane's `editKind` is `Toggle`, it thresholds at `> 0.5f` to produce `0.0f` or `1.0f`. If `minValue` is an integer variant (Note), it rounds using `roundf()`. On patch-default lanes the follow-patch sentinel (`LANE_FOLLOWS_PATCH`, -1) is stored unclamped. `getRawValue()`/`setRawValue()` bypass all of this for save/load.
- **Randomization Algorithm (`randomizeParameters`)**:
  Uses an internal Linear Congruential Generator (LCG). It rewrites the parameter lanes but never the groove: `Gate` and `Slide` are untouched, and amount-0 lanes are left alone.
  - `Note`: Random scale degree 0–12, quantized into the current scale at playback.
  - `Octave` / `GateLength`: At this layer each step is rewritten with `mapNormalizedValueToParamRange(id, 0.5f)`, which is not the neutral value for Octave: it lands in the +1 detent (0.75), and GateLength becomes 0.55 (midpoint of 0.1–1.0). `Sequencer::randomizeParameters()` then writes Octave back to 0.5 across all 64 slots (unless the Octave lane amount is 0); GateLength stays at 0.55.
  - Remaining lanes: Triangular spread centered on the middle of the lane's range (most steps near the middle, a few reaching the depth edge) at the default depth of 35% (`ParameterManager.cpp:157-193`).

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
    int8_t getCurrentNote() const;
    uint8_t getCurrentStepForParameter(ParamId paramId) const;
    Step getStep(uint8_t stepIdx) const;
    void setStep(uint8_t stepIdx, const Step &step);
    void copyStep(uint8_t srcStep, uint8_t dstStep);
    Step getPlaybackStep(uint8_t stepIdx = UINT8_MAX) const;

    // Performance edits (gate-protected Note; return true on a stored change)
    bool recordLiveValue(ParamId id, float value);            // lane's playing step
    bool editStepValue(ParamId id, uint8_t stepIdx, float value); // Step Edit

    // Step Execution & Preview
    void playStepNow(uint8_t stepIdx, VoiceState *voiceState);
    void previewActiveStep(VoiceState *voiceState);
    void toggleStep(uint8_t stepIdx);

    // Transport Control
    void start() { running = true; }
    void stop() { running = false; }
    bool isRunning() const { return running; }
    void randomizeParameters(uint8_t depthPercent = 35, uint64_t seed = 0);

    // Note & Envelope Timing
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState *voiceState);
    bool tickNoteDuration(VoiceState *voiceState);
    bool isNotePlaying() const;
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    // Core Step Advancement (Primitive Signature)
    void advanceStep(uint32_t current_uclock_step, int mm_distance,
                     const StepEditButtons &buttons,
                     int current_selected_step_for_edit,
                     VoiceState *voiceState);
};
```

> **Important**: `Sequencer` does **not** include or accept `UIState`. All UI parameters are unpacked before calling `Sequencer::advanceStep()`.

> **Voice Editing playback transform (2026-09-11)**: `Sequencer` also carries a
> `setPlaybackTransform()` callback (with an optional octave mapper). It is installed only
> during playback so sequenced lanes compose with each voice's edit bases (absolute lanes play their own value or the patch value; Note, Octave and GateLength are offsets on top of the patch)
> without ever rewriting stored steps — see `Sequencer.h` and
> [`docs/voice-edit.md`](voice-edit.md).

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
   - Normalizes the hand: the calibrated value from `setRecordingInput()` (55–700 mm window), else the portable fallback `clamp(mm_distance / MAX_SENSOR_DISTANCE_MM, 0.0f, 1.0f)` with `MAX_SENSOR_DISTANCE_MM = 555.0f` (`Sequencer.cpp`). The firmware overrides the fallback: `processSequencerStep()` in `src/app/StepPlayback.cpp` calls `setRecordingInput(AppState::performanceInput.recordingValue())` on every voice before each advance, and the value is consumed (reset to -1) once per `advanceStep()`.
   - For each held parameter button, maps the value to the parameter's range and calls `recordLiveValue(paramId, value)`, which writes the lane's own playing step (`currentStepPerParam[paramId]`). For `ParamId::Note` it writes only while the playing Gate step is HIGH.
   - This is the step-boundary half of live recording: the new step starts from the hand's current height. Between steps the firmware keeps recording the continuous lanes (Velocity, Filter, Attack, Release; `ControlSurface::recordsBetweenSteps()`) through the same `recordLiveValue()` every control pass (`recordHeldParameters()` and `recordParameter()` in `src/app/StepPlayback.cpp`). It refreshes the voice while its gate is high; edits on rests remain stored until a gated step plays. Note and Octave stay one value per note.
6. **Step Processing (`processStep`)**:
   Calls `processStep(UINT8_MAX, voiceState)` to populate the output `VoiceState`:
   - Extracts all parameter values at their respective `currentStepPerParam[id]` indices.
    - Calculates final note value and clamps to valid MIDI range `[0, 127]`.
    - Converts the octave lane through the injected `octaveMapper_`. The firmware injects `VoiceEdit::mapOctave()` (`src/app/VoiceSetup.cpp`), which gives the five detents below. The portable default, used when no mapper is injected (host tests), is `mapFloatToOctaveOffset()`: only -12, 0 or +12, with thresholds at 1/3 and 2/3 of the lane. Firmware mapping:
      - Stored `0.00` &rarr; `-24` semitones (-2 oct)
      - Stored `0.25` &rarr; `-12` semitones (-1 oct)
      - Stored `0.50` &rarr; `0` semitones (0 oct)
      - Stored `0.75` &rarr; `+12` semitones (+1 oct)
      - Stored `1.00` &rarr; `+24` semitones (+2 oct)
    - The published `noteIndex` stays a raw scale degree (`Sequencer.cpp`: "audio quantizes it"): the audio core maps it through the selected scale row and the one global tuning (`Voice::setTuningPointer()`), so the same stored steps play in 12-EDO or any tuning of the library without being rewritten.
    - Slide Handling: If `!slideVal || !noteActive`, envelope retriggers (`voiceState->shouldRetrigger = true`). If sliding from an already active note (`slideVal && noteActive`), `shouldRetrigger = false` and note frequency transitions smoothly via slewing in `Voice`.
   - Gate-Controlled Voice Output: If Gate is LOW, the previous note, envelope, tone, and other voice settings are retained in `VoiceState`. Only the gate closes, allowing the release tail to finish with the triggering step's settings.

---

## 4. UI Adapter & Firmware Integration

### 4.1 Adapter Function (`advanceSequencerStep`)

The bridge between `UIState` and `Sequencer` is private to `src/app/StepPlayback.cpp`, alongside its caller:

```cpp
void advanceSequencerStep(Sequencer &seq, uint32_t current_uclock_step, int mm_distance,
                          const UIState &uiState, VoiceState *voiceState)
{
  StepEditButtons buttons;
  buttons.note = uiState.parameterButtonHeld[static_cast<int>(ParamId::Note)];
  buttons.velocity = uiState.parameterButtonHeld[static_cast<int>(ParamId::Velocity)];
  buttons.filter = uiState.parameterButtonHeld[static_cast<int>(ParamId::Filter)];
  buttons.attack = uiState.parameterButtonHeld[static_cast<int>(ParamId::Attack)];
  buttons.release = uiState.parameterButtonHeld[static_cast<int>(ParamId::Release)];
  buttons.octave = uiState.parameterButtonHeld[static_cast<int>(ParamId::Octave)];
  seq.advanceStep(current_uclock_step, mm_distance, buttons,
                  uiState.selectedStepForEdit, voiceState);
}
```

### 4.2 Main Step Callback and Control-Loop Drain

`uClock` invokes `onStepCallback()` on Core 0 (timer ISR context) every 16th
note. It only enqueues the step number in the 16-entry SPSC queue in
`src/app/ClockService.cpp`; full queues drop the new step and count the drop.
`processClockEvents()` drains the queue in ordinary control-loop context and
calls `processSequencerStep()` in `src/app/StepPlayback.cpp`.

Step playback iterates `AppState::sequencers` in voice order, advances all four
sequencers, routes hand-distance input only to the selected voice, and publishes the
resulting `VoiceState` snapshots through `VoiceManager`; it contains no encoder
handling. Concrete `seq1`..`seq4` construction remains in
`AppState.cpp`; callers borrow its routing table rather than assemble another.

`processPendingGateTicks()` drains PPQN ticks separately and calls
`Sequencer::tickNoteDuration()` for each voice. On expiry, the updated gate-off
snapshot is published immediately. This is the sole note-duration authority:
there is no `VoiceSystem` gate countdown or two-voice MIDI lifecycle branch.
See [VoiceSystem](VoiceSystem.md#4-step-and-duration-flow) for ownership.

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
parameter track, `VoiceState::isGateHigh`, and sequencer-owned note durations.

---

## 7. Gate-Controlled Note Programming & Slide Operation

### 7.1 Gate-Controlled Note Editing

To prevent accidental modification of pitch parameters on inactive steps during live performance or parameter recording, every performance write goes through one of two gate-aware methods (both return `true` only when the stored value actually changed, so callers refresh the voice only then):

1. **Live recording (`recordLiveValue(id, value)`)** — used by `advanceStep()` on each step and by the firmware between steps (distance sensor with a parameter button held). It writes the lane's playing step and skips `ParamId::Note` while the **playing Gate step** (the Gate lane's cursor, which under polymeter can differ from the Note lane's) is `0.0f`.
2. **Step edit (`editStepValue(id, step, value)`)** — used by the sensor, the ENV-mode faders and the encoder while a step is selected. It skips `ParamId::Note` when that **step's own Gate** is `0.0f`.

Other lanes always take the value. `setStepParameterValue()` itself stays a plain clamped write for programmatic use (randomize, presets, tests, `setStep()`); persistence uses `setRawStepValue()`.

### 7.2 Slide / Portamento Logic

When a step has `hasSlide = true`:
1. `voiceState->shouldRetrigger = false`: The ADSR envelope is **not** retriggered, allowing the note to sustain continuously.
2. `noteDuration.start(noteDurationTicks)`: The note duration timer is refreshed for the new step.
3. `previousStepHadSlide`: If a gate-off step immediately follows a slide step, `handleNoteOff()` is bypassed, so only the sequencer's note bookkeeping (`noteActive`, the note-duration timer) persists through the rest. The rest still publishes `isGateHigh = false`, so the voice's gate drops, the envelope enters release, and the next rising gate restarts the envelope (`Voice::handleGateEdges_()`). A slide therefore does not carry a sustained note across a rest.
4. Pitch slewing is performed inside `Voice::processFrequencySlew()` using exponential filter coefficient `slideAlpha = 1.0f - std::exp(-1.0f / (slideTimeSeconds * sampleRate))`.

---

## 8. Summary of Data Structures & State Containers

| Struct / Class | Location | Primary Purpose |
|---|---|---|
| `Sequencer` | `src/pico2seq-core/sequencer/Sequencer.h/.cpp` | Core step sequencer logic, parameter automation, note lifecycle |
| `ParameterManager` | `src/pico2seq-core/sequencer/ParameterManager.h/.cpp` | 11 independent `ParameterTrack<64>` instances, value clamping, randomization |
| `ParameterTrack<64>` | `src/rpdsp/src/rpdsp/parameter_track.h` | Fixed-size polymetric track with modulo wrapping |
| `VoiceState` | `src/pico2seq-core/sequencer/SequencerDefs.h` | Control snapshot emitted on steps and note-duration expiry to configure `Voice` DSP |
| `Step` | `src/pico2seq-core/sequencer/SequencerDefs.h` | Internal parameter snapshot for step editing and inspection |
| `ShuffleTemplate` | `src/pico2seq-core/sequencer/ShuffleTemplates.h` | 16-step microtiming offsets for 480 PPQN groove templates |
| `advanceSequencerStep` | `src/app/StepPlayback.cpp` | Private firmware UI adapter bridging `UIState` to `Sequencer::advanceStep` |
