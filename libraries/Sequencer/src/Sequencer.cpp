#include <cstdint>
#include <algorithm>
#include <cmath>
#include "SequencerDefs.h"
#include "Sequencer.h"
#include "Arduino.h"

// External mode state variables
extern bool slideMode;
// --- Constants for real-time parameter editing ---
constexpr float MAX_SENSOR_DISTANCE_MM = 1400.0f;
constexpr float MAX_NOTE_PARAM_RANGE = 24.0f;  // e.g., for mapping sensor to a 2-octave range for note param
constexpr float OCTAVE_LOW_THRESHOLD = .15f; // Threshold for mapping float to -1 octave
constexpr float OCTAVE_HIGH_THRESHOLD = .4f; // Threshold for mapping float to +1 octave
int8_t mapFloatToOctaveOffset(float octaveValue)
{
    if (octaveValue < OCTAVE_LOW_THRESHOLD)
    {
        return -12;
    }
    else if (octaveValue > OCTAVE_HIGH_THRESHOLD)
    {
        return 12;
    }
    else
    {
        return 0;
    }
}

// Helper function to map a normalized value (0.0-1.0) to a parameter's defined min/max range.
float mapNormalizedValueToParamRange(ParamId id, float normalizedValue)
{
    const auto &def = CORE_PARAMETERS[static_cast<size_t>(id)];

    // Safely get float from the variant, regardless of underlying type (int, float, bool)
    auto get_float = [](const ParameterValueType &v) -> float
    {
        return std::visit(
            [](auto &&arg) -> float
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) return arg;
                if constexpr (std::is_same_v<T, int>) return static_cast<float>(arg);
                if constexpr (std::is_same_v<T, bool>) return arg ? 1.0f : 0.0f;
                return 0.0f; // Fallback
            },
            v);
    };

    float minVal = get_float(def.minValue);
    float maxVal = get_float(def.maxValue);

    // The ParameterManager's setValue will handle clamping and rounding.
    return minVal + normalizedValue * (maxVal - minVal);
}

Sequencer::Sequencer()
    : running(false), currentStep(0), lastNote(-1), currentNote(-1), noteDurationCounter(0), channel(0), parameterManager(), previousStepHadSlide(false) // Initialize parameterManager explicitly
      ,
      envelope() // Initialize envelope explicitly
      ,
      noteDuration() // Initialize noteDuration explicitly
{
    // Initialize all per-parameter step counters to 0
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        currentStepPerParam[i] = 0;
    }
    
    // Initialize GPIO pins for gate outputs and step clock
    pinMode(10, OUTPUT);  // Voice 1 gate output
    pinMode(11, OUTPUT);  // Voice 2 gate output
    pinMode(12, OUTPUT);  // Step clock output
    digitalWrite(10, LOW);
    digitalWrite(11, LOW);
    digitalWrite(12, LOW);
    
    initializeParameters();
}

Sequencer::Sequencer(uint8_t channel)
    : running(false), currentStep(0), lastNote(-1), currentNote(-1), noteDurationCounter(0), channel(channel), parameterManager(), previousStepHadSlide(false) // Initialize parameterManager explicitly
      ,
      envelope() // Initialize envelope explicitly
      ,
      noteDuration() // Initialize noteDuration explicitly
{
    // Initialize all per-parameter step counters to 0
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        currentStepPerParam[i] = 0;
    }
    
    // Initialize GPIO pins for gate outputs and step clock
    pinMode(10, OUTPUT);  // Voice 1 gate output
    pinMode(11, OUTPUT);  // Voice 2 gate output
    pinMode(12, OUTPUT);  // Step clock output
    digitalWrite(10, LOW);
    digitalWrite(11, LOW);
    digitalWrite(12, LOW);
    
    initializeParameters();
}

bool Sequencer::isNotePlaying() const
{
    // A note is considered playing while the envelope is either currently triggered
    // (gate held) OR it's in the release phase (release requested but envelope not finished).
    // Using logical OR ensures the envelope's release phase is treated as "playing"
    // so the voice won't continue indefinitely after a high gate event.
    return envelope.isTriggered() || !envelope.isReleased();
}

void Sequencer::initializeParameters()
{
    parameterManager.init();
}

void Sequencer::setParameterStepCount(ParamId id, uint8_t steps)
{
    parameterManager.setStepCount(id, steps);
}

uint8_t Sequencer::getParameterStepCount(ParamId id) const
{
    return parameterManager.getStepCount(id);
}

float Sequencer::getStepParameterValue(ParamId id, uint8_t stepIdx) const
{
    return parameterManager.getValue(id, stepIdx);
}

void Sequencer::setStepParameterValue(ParamId id, uint8_t stepIdx, float value)
{
    // GATE-CONTROLLED NOTE PROGRAMMING: Prevent Note parameter changes on steps with LOW gates
    if (id == ParamId::Note)
    {
        // Check if the step's gate is HIGH before allowing note programming
        float gateValue = getStepParameterValue(ParamId::Gate, stepIdx);
        if (gateValue <= 0.5f) // Gate is LOW (0.0)
        {
            // Silently ignore note programming attempts on steps with LOW gates
            // This protects steps from note frequency changes during programming/editing
            return;
        }
    }

    parameterManager.setValue(id, stepIdx, value);
}

void Sequencer::reset()
{
    currentStep = 0;
    // Reset all per-parameter step counters to 0
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        currentStepPerParam[i] = 0;
    }
    running = false;
    previousStepHadSlide = false; // Reset slide state tracking
    handleNoteOff(nullptr); // Pass nullptr as no voice state to update
}

uint8_t Sequencer::getCurrentStepForParameter(ParamId paramId) const
{
    return currentStepPerParam[static_cast<size_t>(paramId)];
}

void Sequencer::resetAllSteps()
{
    // Data-driven reset using the defaults from CORE_PARAMETERS
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        ParamId currentId = static_cast<ParamId>(i);
        float defaultValue = std::visit(
            [](auto &&arg) -> float
            {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) return arg;
                if constexpr (std::is_same_v<T, int>) return static_cast<float>(arg);
                if constexpr (std::is_same_v<T, bool>) return arg ? 1.0f : 0.0f;
                return 0.0f; // Fallback
            },
            CORE_PARAMETERS[i].defaultValue);

        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
        {
            setStepParameterValue(currentId, step, defaultValue);
        }
    }
}
void Sequencer::advanceStep(uint8_t current_uclock_step, int mm_distance,
                            bool is_note_button_held, bool is_velocity_button_held,
                            bool is_filter_button_held, bool is_attack_button_held,
                            bool is_decay_button_held, bool is_octave_button_held,
                            int current_selected_step_for_edit,
                             VoiceState *voiceState)
{
    if (!running)
    {
        return;
    }

    // Output step clock pulse on pin 12 (with swing timing from uClock)
    // This triggers on every step regardless of gate state
    digitalWrite(12, HIGH);
    digitalWrite(12, LOW);

    // Use the Gate parameter's step count to determine the main sequence length
    uint8_t sequenceLength = getParameterStepCount(ParamId::Gate);
    if (sequenceLength > 0)
    {
        currentStep = current_uclock_step % sequenceLength;
    }
    else
    {
        currentStep = 0; // Fallback if no sequence length is set
    }

    // Advance each parameter's step counter independently based on its own step count
    // This enables polyrhythmic patterns where different parameters cycle at different rates
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        ParamId paramId = static_cast<ParamId>(i);
        uint8_t paramStepCount = getParameterStepCount(paramId);

        if (paramStepCount > 0)
        {
            // Use efficient increment and wrap instead of modulo for performance
            currentStepPerParam[i] = current_uclock_step % paramStepCount;
        }
        else
        {
            currentStepPerParam[i] = 0; // Fallback if no step count is set
        }
    }

    // Track if any parameters were recorded during this step
    bool parametersRecorded = false;

    // Handle real-time parameter recording first
    // Disable distance sensor control when in edit mode (selectedStepForEdit >= 0)
    if (mm_distance >= 0 && current_selected_step_for_edit == -1)
    {
        // Simple normalization of sensor distance value
        const float normalizedDistance = std::max(0.0f, std::min(static_cast<float>(mm_distance) / MAX_SENSOR_DISTANCE_MM, 1.0f));

        struct ParamButton
        {
            ParamId id;
            bool held;
        };
        const ParamButton paramButtons[] = {
            {ParamId::Note, is_note_button_held},
            {ParamId::Velocity, is_velocity_button_held},
            {ParamId::Filter, is_filter_button_held},
            {ParamId::Attack, is_attack_button_held},
            {ParamId::Decay, is_decay_button_held},
            {ParamId::Octave, is_octave_button_held}
            // Removed: {ParamId::Slide, slideMode}
            // This was causing slide values to be overwritten during playback in slide mode.
            // Slide values should only be set via step presses in slide mode, not real-time recording.
        };

        for (const auto &pb : paramButtons)
        {
            if (pb.held)
            {
                // GATE-CONTROLLED NOTE PROGRAMMING: Check gate restriction for Note parameter
                if (pb.id == ParamId::Note)
                {
                    uint8_t paramStepIdx = currentStepPerParam[static_cast<size_t>(pb.id)];
                    float gateValue = getStepParameterValue(ParamId::Gate, paramStepIdx);
                    if (gateValue <= 0.5f) // Gate is LOW (0.0)
                    {
                        // Skip Note parameter recording on steps with LOW gates
                        continue;
                    }
                }

                parametersRecorded = true;

                // For all other parameters, scale the normalized value to the parameter's range
                float value = mapNormalizedValueToParamRange(pb.id, normalizedDistance);
                // Use the parameter's own current step index for recording
                uint8_t paramStepIdx = currentStepPerParam[static_cast<size_t>(pb.id)];
                setStepParameterValue(pb.id, paramStepIdx, value);
            }
        }
    }

    // Process the step with current parameter values (including any newly recorded ones)
    // Use UINT8_MAX to signal that per-parameter step indices should be used
    processStep(UINT8_MAX, voiceState);

    // If parameters were recorded during this step, the voice state is already updated
    // with the new values by processStep() above, providing immediate real-time feedback
}

void Sequencer::advanceStep(uint8_t current_uclock_step, int mm_distance,
                            const UIState& uiState, VoiceState *voiceState)
{
    // Extract button states from UIState and call the main advanceStep method
    advanceStep(current_uclock_step, mm_distance,
                uiState.parameterButtonHeld[static_cast<int>(ParamId::Note)],
                uiState.parameterButtonHeld[static_cast<int>(ParamId::Velocity)],
                uiState.parameterButtonHeld[static_cast<int>(ParamId::Filter)],
                uiState.parameterButtonHeld[static_cast<int>(ParamId::Attack)],
                uiState.parameterButtonHeld[static_cast<int>(ParamId::Decay)],
                uiState.parameterButtonHeld[static_cast<int>(ParamId::Octave)],
                uiState.selectedStepForEdit,
                voiceState);
}

void Sequencer::processStep(uint8_t stepIdx, VoiceState *voiceState)
{
    // This method supports two modes:
    // 1. When stepIdx == UINT8_MAX, use per-parameter step indices (called from advanceStep)
    // 2. When stepIdx is a valid index, use that index for all parameters (called from playStepNow)
    bool usePerParameterIndices = (stepIdx == UINT8_MAX);

    if (voiceState)
    {
        voiceState->shouldRetrigger = false; // Always reset the retrigger flag at the start
    }

    // Get parameter values using appropriate step indices
    float gateOn = getStepParameterValue(ParamId::Gate,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Gate)] : stepIdx);

    // Always get parameter values for modulation parameters
    float filterVal = getStepParameterValue(ParamId::Filter,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Filter)] : stepIdx);
    float attackVal = getStepParameterValue(ParamId::Attack,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Attack)] : stepIdx);
    float decayVal = getStepParameterValue(ParamId::Decay,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Decay)] : stepIdx);

    // Get note-related parameters
    uint8_t noteStepIdx = usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Note)] : stepIdx;
    float noteVal = getStepParameterValue(ParamId::Note, noteStepIdx);
    float velocityVal = getStepParameterValue(ParamId::Velocity,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Velocity)] : stepIdx);
    float octaveFloat = getStepParameterValue(ParamId::Octave,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Octave)] : stepIdx);
    float slideVal = getStepParameterValue(ParamId::Slide,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::Slide)] : stepIdx);

    // DEBUG: Trace parameter retrieval
    /*
    Serial.print("[PARAM DEBUG] Step ");
    Serial.print(stepIdx);
    Serial.print(" (noteStepIdx: ");
    Serial.print(noteStepIdx);
    Serial.print(") - Retrieved noteVal: ");
    Serial.print(noteVal, 2);
    Serial.print(", octaveFloat: ");
    Serial.print(octaveFloat, 2);
    Serial.println();
    */
    float gateLengthProportion = getStepParameterValue(ParamId::GateLength,
        usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(ParamId::GateLength)] : stepIdx);



    const uint16_t noteDurationTicks = static_cast<uint16_t>(std::max(1.0f, gateLengthProportion * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
    const int8_t octaveOffset = mapFloatToOctaveOffset(octaveFloat);


    if (gateOn)
    {
        // Calculate the final note value
        int finalNote = noteVal + octaveOffset;

        // If the step's gate is on, decide whether to start a new note or slide to it.
        if (!slideVal)
        {
            // This is a non-sliding note. Always retrigger envelope for each gated step.
            if (voiceState)
            {
                voiceState->shouldRetrigger = true;
            }

            // Trigger note with velocity scaled to MIDI range (0-127)
            // Output gate signal based on channel
             if (channel == 1) {
                 digitalWrite(10, HIGH);  // Voice 1 gate output
             } else if (channel == 2) {
                 digitalWrite(11, HIGH);  // Voice 2 gate output
             }
            
            startNote(static_cast<uint8_t>(finalNote), static_cast<uint8_t>(velocityVal * 127.0f),
                      noteDurationTicks);
        }
        else
        {
            // This is a slide. Don't retrigger the envelope, just update the current note value.
            currentNote = static_cast<int8_t>(finalNote);
            // For slides, we still need to update the note duration for the current step
            noteDuration.start(noteDurationTicks);
        }
    }
    else
    {     if (channel == 1) {
                 digitalWrite(10, LOW);  // Voice 1 gate output
             } else if (channel == 2) {
                 digitalWrite(11, LOW);  // Voice 2 gate output
             }
        // The gate is off for this step. Only turn off the note if the previous step didn't have slide enabled.
        // This allows slide steps to sustain the envelope even when followed by gate-off steps.
        if (!previousStepHadSlide)
        {
            handleNoteOff(voiceState);
        }
    }

    // Add a null check to prevent crashes when previewing steps without a valid voiceState
    if (voiceState)
    {
        // Always update non-frequency parameters regardless of gate state
        voiceState->filterCutoff = filterVal;
        voiceState->attackTimeSeconds = attackVal;
        voiceState->decayTimeSeconds = decayVal;
        voiceState->velocityLevel = velocityVal;
        voiceState->isGateHigh = gateOn;
        voiceState->hasSlide = slideVal;
        voiceState->gateLengthTicks = noteDurationTicks;

        // GATE-CONTROLLED NOTE OUTPUT: Only update note and octave when gate is HIGH
        // This allows current notes to continue/fade when gate is LOW
        if (gateOn)
        {
            voiceState->noteIndex = noteVal; // Store raw note value for audio synthesis (scale array lookup)
            voiceState->octaveOffset = octaveOffset;
        }
        // When gate is LOW, preserve the previous note and octave values
        // allowing the current note to continue playing or fade naturally
    }

    // Update slide state tracking for next step
    previousStepHadSlide = slideVal && gateOn;
}

void Sequencer::startNote(uint8_t note, uint8_t velocity, uint16_t duration)
{
    // Update note state
    currentNote = static_cast<int8_t>(note);
    lastNote = currentNote;

    // Start duration tracking and envelope
    noteDuration.start(duration);
    triggerEnvelope();
}

void Sequencer::handleNoteOff( VoiceState* voiceState)
{
    if (currentNote >= 0)
    {
        // Send MIDI note-off if callback is set
        if (midiNoteOffCallback)
        {
            midiNoteOffCallback(static_cast<uint8_t>(currentNote), channel);
        }

        // Ensure hardware gate outputs are forced low when a note is turned off.
        if (channel == 1)
        {
            digitalWrite(10, LOW); // Voice 1 gate output
        }
        else if (channel == 2)
        {
            digitalWrite(11, LOW); // Voice 2 gate output
        }
        
        currentNote = -1;
        releaseEnvelope();
        noteDuration.reset();

        // If a voiceState is provided, update it to signal note-off to the audio engine.
        if (voiceState)
        {
            voiceState->isGateHigh = false;
            voiceState->shouldRetrigger = false;
        }
    }
}

void Sequencer::tickNoteDuration( VoiceState* voiceState)
{
    if (noteDuration.isActive())
    {
        noteDuration.tick();
        if (!noteDuration.isActive())
        {
            // Note duration has expired, turn the note off.
            handleNoteOff(voiceState);
        }
    }
}

void Sequencer::playStepNow(uint8_t stepIdx, VoiceState *voiceState)
{
    // This method is the public entry point for previewing a step.
    // It processes the step's parameters, updates the provided voice state,
    // and triggers the note's envelope.
    processStep(stepIdx, voiceState);
}
void Sequencer::toggleStep(uint8_t stepIdx)
{
    // Get current gate value
    float gate = getStepParameterValue(ParamId::Gate, stepIdx);
    // Toggle: if >0.5, set to 0.0; else set to 1.0
    setStepParameterValue(ParamId::Gate, stepIdx, (gate > 0.5f) ? 0.0f : 1.0f);
}

Step Sequencer::getStep(uint8_t stepIdx) const
{
    Step s;
    s.noteIndex = getStepParameterValue(ParamId::Note, stepIdx);
    s.velocityLevel = getStepParameterValue(ParamId::Velocity, stepIdx);
    s.filterCutoff = getStepParameterValue(ParamId::Filter, stepIdx);
    s.attackTimeSeconds = getStepParameterValue(ParamId::Attack, stepIdx);
    s.decayTimeSeconds = getStepParameterValue(ParamId::Decay, stepIdx);
    s.isGateActive = getStepParameterValue(ParamId::Gate, stepIdx) > 0.5f;
    s.hasSlide = getStepParameterValue(ParamId::Slide, stepIdx) > 0.5f;
    s.octaveOffset = mapFloatToOctaveOffset(getStepParameterValue(ParamId::Octave, stepIdx));

    const float gateLengthProportion = getStepParameterValue(ParamId::GateLength, stepIdx);
    s.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f, gateLengthProportion * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
    return s;
}

void Sequencer::randomizeParameters()
{
    parameterManager.randomizeParameters();
    for (size_t i = 0; i < 16; ++i){
setStepParameterValue(ParamId::Octave, i, 0.0f);
setStepParameterValue(ParamId::Attack, i, 0.001f);
setStepParameterValue(ParamId::Decay, i, 0.12f);


    }
   // setParameterStepCount(ParamId::Octave, random(2,8));
    
}

void Sequencer::triggerEnvelope()
{
    envelope.trigger();
}

void Sequencer::releaseEnvelope()
{
    envelope.release();
}

void Sequencer::setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel))
{
    midiNoteOffCallback = callback;
}
