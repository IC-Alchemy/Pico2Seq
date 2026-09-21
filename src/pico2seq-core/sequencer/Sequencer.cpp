#include <cstdint>
#include <algorithm>
#include <cmath>
#include "SequencerDefs.h"
#include "Sequencer.h"

// --- Constants for real-time parameter editing ---
constexpr float MAX_SENSOR_DISTANCE_MM = 750.0f;

constexpr float OCTAVE_LOW_THRESHOLD = 1.0f / 3.0f;  // Threshold for mapping float to -1 octave
constexpr float OCTAVE_HIGH_THRESHOLD = 2.0f / 3.0f;  // Threshold for mapping float to +1 octave
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

// Fold a variant parameter value (int, float, or bool) into the float domain
// the parameter tracks store.
float parameterValueAsFloat(const ParameterValueType &value)
{
    return std::visit(
        [](auto &&arg) -> float
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>)
                return arg;
            if constexpr (std::is_same_v<T, int>)
                return static_cast<float>(arg);
            if constexpr (std::is_same_v<T, bool>)
                return arg ? 1.0f : 0.0f;
            return 0.0f; // Fallback for unexpected types
        },
        value);
}

// Helper function to map a normalized value (0.0-1.0) to a parameter's defined min/max range.
float mapNormalizedValueToParamRange(ParamId id, float normalizedValue)
{
    if (id == ParamId::Octave)
    {
        if (normalizedValue <= SequencerConstants::OCTAVE_NORM_MINUS_2_MAX)
            return SequencerConstants::OCTAVE_TRACK_MINUS_2;
        if (normalizedValue <= SequencerConstants::OCTAVE_NORM_MINUS_1_MAX)
            return SequencerConstants::OCTAVE_TRACK_MINUS_1;
        if (normalizedValue <= SequencerConstants::OCTAVE_NORM_ZERO_MAX)
            return SequencerConstants::OCTAVE_TRACK_ZERO;
        if (normalizedValue <= SequencerConstants::OCTAVE_NORM_PLUS_1_MAX)
            return SequencerConstants::OCTAVE_TRACK_PLUS_1;
        return SequencerConstants::OCTAVE_TRACK_PLUS_2;
    }

    const auto &def = CORE_PARAMETERS[static_cast<size_t>(id)];
    const float minVal = parameterValueAsFloat(def.minValue);
    const float maxVal = parameterValueAsFloat(def.maxValue);

    // The ParameterManager's setValue will handle clamping and rounding.
    return minVal + normalizedValue * (maxVal - minVal);
}

Sequencer::Sequencer()
    : Sequencer(0)
{
}

Sequencer::Sequencer(uint8_t channel)
    : running(false), currentStep(0), lastNote(-1), currentNote(-1), noteDurationCounter(0), channel(channel), parameterManager(), previousStepHadSlide(false) // Initialize parameterManager explicitly
      ,
      envelope() // Initialize envelope explicitly
      ,
      noteDuration() // Initialize noteDuration explicitly
{
    noteActive = false;
    // Initialize all per-parameter step counters to 0
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        currentStepPerParam[i] = 0;
    }

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

float Sequencer::getRawStepValue(ParamId id, uint8_t stepIdx) const
{
    return parameterManager.getRawValue(id, stepIdx);
}

void Sequencer::setRawStepValue(ParamId id, uint8_t stepIdx, float value)
{
    parameterManager.setRawValue(id, stepIdx, value);
}

void Sequencer::setStepParameterValue(ParamId id, uint8_t stepIdx, float value)
{
    parameterManager.setValue(id, stepIdx, value);
}

bool Sequencer::gateIsOn(uint8_t stepIdx) const
{
    return getStepParameterValue(ParamId::Gate, stepIdx) > 0.5f;
}

bool Sequencer::writeStepValue(ParamId id, uint8_t stepIdx, float value)
{
    const float previous = getStepParameterValue(id, stepIdx);
    setStepParameterValue(id, stepIdx, value);
    return getStepParameterValue(id, stepIdx) != previous;
}

bool Sequencer::recordLiveValue(ParamId id, float value)
{
    if (id >= ParamId::Count)
    {
        return false;
    }
    // GATE-CONTROLLED NOTE PROGRAMMING: pitch lands only while the playing
    // gate is on. Under polymeter that is the Gate lane's cursor, not Note's.
    if (id == ParamId::Note && !gateIsOn(getCurrentStepForParameter(ParamId::Gate)))
    {
        return false;
    }
    return writeStepValue(id, getCurrentStepForParameter(id), value);
}

bool Sequencer::editStepValue(ParamId id, uint8_t stepIdx, float value)
{
    if (id >= ParamId::Count || stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
    {
        return false;
    }
    if (id == ParamId::Note && !gateIsOn(stepIdx))
    {
        return false;
    }
    return writeStepValue(id, stepIdx, value);
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
    handleNoteOff(nullptr);       // Pass nullptr as no voice state to update
}

uint8_t Sequencer::getCurrentStepForParameter(ParamId paramId) const
{
    return currentStepPerParam[static_cast<size_t>(paramId)];
}

void Sequencer::resetAllSteps()
{
    if(usesPlaybackTransform()) {
        for(uint8_t step=0;step<SequencerConstants::MAX_STEPS_COUNT;++step) resetModifierStep(step);
        return;
    }
    // Data-driven reset using the defaults from CORE_PARAMETERS
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        ParamId currentId = static_cast<ParamId>(i);
        const float defaultValue = parameterValueAsFloat(CORE_PARAMETERS[i].defaultValue);

        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
        {
            parameterManager.setValue(currentId, step, defaultValue);
        }
    }
}

void Sequencer::clearPattern()
{
    if (usesPlaybackTransform())
    {
        // resetModifierStep writes through setValue, which wraps at the active
        // track length: widen every track first so the neutral modifiers land
        // in all 64 slots and survive later track growth.
        for (uint8_t param = 0; param < PARAM_ID_COUNT; ++param)
        {
            parameterManager.setStepCount(static_cast<ParamId>(param),
                                          SequencerConstants::MAX_STEPS_COUNT);
        }
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
        {
            resetModifierStep(step);
        }
    }
    else
    {
        // Re-run the boot initialization: it fills the full 64-slot capacity
        // with each track's default value, not just the active length.
        initializeParameters();
    }
    // Restore the default polyrhythm track lengths (16 steps everywhere).
    for (uint8_t param = 0; param < PARAM_ID_COUNT; ++param)
    {
        parameterManager.setStepCount(static_cast<ParamId>(param),
                                      CORE_PARAMETERS[param].defaultSteps);
    }
    // End a sounding note the same way reset() does.
    handleNoteOff(nullptr);
}
void Sequencer::advanceStep(uint32_t current_uclock_step, int mm_distance,
                            bool is_note_button_held, bool is_velocity_button_held,
                            bool is_filter_button_held, bool is_attack_button_held,
                            bool is_release_button_held, bool is_octave_button_held,
                            int current_selected_step_for_edit,
                            VoiceState *voiceState)
{
    if (!running)
    {
        return;
    }

    // Use the Gate parameter's step count to determine the main sequence length
    uint8_t sequenceLength = getParameterStepCount(ParamId::Gate);
    if (sequenceLength > 0)
    {
        // Modulo the full-width counter first; narrowing before the modulo would
        // alias every 256 steps (uint8_t wrap) onto pattern position 0.
        currentStep = static_cast<uint8_t>(current_uclock_step % sequenceLength);
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
            currentStepPerParam[i] = static_cast<uint8_t>(current_uclock_step % paramStepCount);
        }
        else
        {
            currentStepPerParam[i] = 0; // Fallback if no step count is set
        }
    }

    // Handle real-time parameter recording first
    // Disable distance sensor control when in edit mode (selectedStepForEdit >= 0)
    if (mm_distance >= 0 && current_selected_step_for_edit == -1)
    {
        // Simple normalization of sensor distance value
        const float normalizedDistance = std::clamp(recordingInput_ >= 0.0f ? recordingInput_ :
            static_cast<float>(mm_distance) / MAX_SENSOR_DISTANCE_MM, 0.0f, 1.0f);

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
            {ParamId::Release, is_release_button_held},
            {ParamId::Octave, is_octave_button_held}
            // Removed: {ParamId::Slide, slideMode}
            // This was causing slide values to be overwritten during playback in slide mode.
            // Slide values should only be set via step presses in slide mode, not real-time recording.
        };

        for (const auto &pb : paramButtons)
        {
            if (pb.held)
            {
                // Each lane records at its own cursor; Note skips LOW gates.
                recordLiveValue(pb.id, mapNormalizedValueToParamRange(pb.id, normalizedDistance));
            }
        }
    }

    // Process the step with current parameter values (including any newly recorded ones)
    // Use UINT8_MAX to signal that per-parameter step indices should be used
    recordingInput_ = -1.0f;
    processStep(UINT8_MAX, voiceState);

    // If parameters were recorded during this step, the voice state is already updated
    // with the new values by processStep() above, providing immediate real-time feedback
}

void Sequencer::processStep(uint8_t stepIdx, VoiceState *voiceState)
{
    if (voiceState)
    {
        voiceState->shouldRetrigger = false; // Always reset the retrigger flag at the start
    }

    const Step values = getPlaybackStep(stepIdx);
    const bool gateOn = values.isGateActive;
    const float filterVal = values.filterCutoff;
    const float attackVal = values.attackTimeSeconds;
    const float decayVal = values.decayTimeSeconds;
    const float sustainVal = values.sustainLevel;
    const float releaseVal = values.releaseTimeSeconds;
    const float noteVal = values.noteIndex;
    const float velocityVal = values.velocityLevel;
    const bool slideVal = values.hasSlide;
    const uint16_t noteDurationTicks = values.gateLengthTicks;
    const int8_t octaveOffset = values.octaveOffset;

    if (gateOn)
    {
        // Calculate the final note value and clamp to valid MIDI range [0, 127]
        int rawNote = static_cast<int>(noteVal) + octaveOffset;
        int finalNote = std::max(0, std::min(rawNote, 127));

        // If the step's gate is on, decide whether to start a new note or slide to it.
        // If a slide note is encountered but no note is currently active, start the note.
        if (!slideVal || !noteActive)
        {
            // Always retrigger envelope for each gated step (or initial slide note).
            if (voiceState)
            {
                voiceState->shouldRetrigger = true;
            }

            startNote(static_cast<uint8_t>(finalNote), static_cast<uint8_t>(velocityVal * 127.0f),
                      noteDurationTicks);
        }
        else
        {
            // This is a slide from an active note. Don't retrigger the envelope, just update the current note value.
            currentNote = static_cast<int8_t>(finalNote);
            noteActive = true;
            // For slides, we still need to update the note duration for the current step
            noteDuration.start(noteDurationTicks);
        }
    }
    else
    {
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
        voiceState->sustainLevel = sustainVal;
        voiceState->releaseTimeSeconds = releaseVal;
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
    noteActive = true;
    lastNote = currentNote;

    // Start duration tracking and envelope
    noteDuration.start(duration);
    triggerEnvelope();
}

void Sequencer::handleNoteOff(VoiceState *voiceState)
{
    if (noteActive)
    {
        // Send MIDI note-off if callback is set
        if (midiNoteOffCallback)
        {
            midiNoteOffCallback(static_cast<uint8_t>(currentNote), channel);
        }

        currentNote = -1;
        noteActive = false;
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

bool Sequencer::tickNoteDuration(VoiceState *voiceState)
{
    if (noteDuration.isActive())
    {
        noteDuration.tick();
        if (!noteDuration.isActive())
        {
            // Note duration has expired, turn the note off.
            handleNoteOff(voiceState);
            return true;
        }
    }
    return false;
}

void Sequencer::playStepNow(uint8_t stepIdx, VoiceState *voiceState)
{
    // This method is the public entry point for previewing a step.
    // It processes the step's parameters, updates the provided voice state,
    // and triggers the note's envelope.
    processStep(stepIdx, voiceState);
}

void Sequencer::previewActiveStep(VoiceState *voiceState)
{
    // Evaluates current parameter values at their independent polymetric cursors (UINT8_MAX)
    processStep(UINT8_MAX, voiceState);
}

void Sequencer::refreshVoiceParameters(VoiceState *voiceState) const
{
    if (!voiceState)
    {
        return;
    }
    const Step values = getPlaybackStep();
    voiceState->velocityLevel = values.velocityLevel;
    voiceState->filterCutoff = values.filterCutoff;
    voiceState->attackTimeSeconds = values.attackTimeSeconds;
    voiceState->decayTimeSeconds = values.decayTimeSeconds;
    voiceState->sustainLevel = values.sustainLevel;
    voiceState->releaseTimeSeconds = values.releaseTimeSeconds;
    // Pitch follows only a sounding note, matching processStep(): a released
    // note keeps its pitch through the tail.
    if (voiceState->isGateHigh)
    {
        voiceState->noteIndex = values.noteIndex;
        voiceState->octaveOffset = values.octaveOffset;
    }
    voiceState->shouldRetrigger = false;
}

void Sequencer::toggleStep(uint8_t stepIdx)
{
    // Get current gate value
    float gate = getStepParameterValue(ParamId::Gate, stepIdx);
    // Toggle: if >0.5, set to 0.0; else set to 1.0
    setStepParameterValue(ParamId::Gate, stepIdx, (gate > 0.5f) ? 0.0f : 1.0f);
}

namespace
{
// Share conversions only; the accessor owns cursor selection and playback mapping.
// A concrete callable keeps decoding allocation-free without type erasure.
template <typename ValueAccessor>
Step decodeStep(const ValueAccessor &value, Sequencer::OctaveMapper octaveMapper)
{
    Step s;
    s.noteIndex = value(ParamId::Note);
    s.velocityLevel = value(ParamId::Velocity);
    s.filterCutoff = value(ParamId::Filter);
    s.attackTimeSeconds = value(ParamId::Attack);
    s.decayTimeSeconds = value(ParamId::Decay);
    s.sustainLevel = value(ParamId::Sustain);
    s.releaseTimeSeconds = value(ParamId::Release);
    s.isGateActive = value(ParamId::Gate) > 0.5f;
    s.hasSlide = value(ParamId::Slide) > 0.5f;
    const float octave = value(ParamId::Octave);
    s.octaveOffset = octaveMapper ? octaveMapper(octave) : mapFloatToOctaveOffset(octave);
    s.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f,
        value(ParamId::GateLength) * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
    return s;
}
} // namespace

Step Sequencer::getStep(uint8_t stepIdx) const
{
    const auto value = [&](ParamId id) {
        return getStepParameterValue(id, stepIdx);
    };
    return decodeStep(value, octaveMapper_);
}

void Sequencer::setStep(uint8_t stepIdx, const Step &step)
{
    if (stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
    {
        return;
    }
    setStepParameterValue(ParamId::Note, stepIdx, step.noteIndex);
    setStepParameterValue(ParamId::Velocity, stepIdx, step.velocityLevel);
    setStepParameterValue(ParamId::Filter, stepIdx, step.filterCutoff);
    setStepParameterValue(ParamId::Attack, stepIdx, step.attackTimeSeconds);
    setStepParameterValue(ParamId::Decay, stepIdx, step.decayTimeSeconds);
    setStepParameterValue(ParamId::Sustain, stepIdx, step.sustainLevel);
    setStepParameterValue(ParamId::Release, stepIdx, step.releaseTimeSeconds);
    setStepParameterValue(ParamId::Gate, stepIdx, step.isGateActive ? 1.0f : 0.0f);
    setStepParameterValue(ParamId::Slide, stepIdx, step.hasSlide ? 1.0f : 0.0f);

    const float octaveVal = std::clamp(static_cast<float>(step.octaveOffset) / 48.0f + 0.5f, 0.0f, 1.0f);
    setStepParameterValue(ParamId::Octave, stepIdx, octaveVal);

    const float gateLenFraction = static_cast<float>(step.gateLengthTicks) /
        static_cast<float>(SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS);
    setStepParameterValue(ParamId::GateLength, stepIdx, std::clamp(gateLenFraction, 0.001f, 1.0f));
}

void Sequencer::copyStep(uint8_t srcStep, uint8_t dstStep)
{
    if (srcStep >= SequencerConstants::MAX_STEPS_COUNT || dstStep >= SequencerConstants::MAX_STEPS_COUNT)
    {
        return;
    }
    parameterManager.copyStep(srcStep, dstStep);
}

Step Sequencer::getPlaybackStep(uint8_t stepIdx) const
{
    const auto value = [&](ParamId id) {
        return playbackValue(id, stepIdx == UINT8_MAX ? getCurrentStepForParameter(id) : stepIdx);
    };
    return decodeStep(value, octaveMapper_);
}

void Sequencer::randomizeParameters(uint8_t depthPercent, uint64_t seed)
{
    parameterManager.randomizeParameters(depthPercent, seed);
    if (usesPlaybackTransform())
    {
        // The draws are offsets around 0.5; absolute lanes store the value
        // they play, spread around the patch value like the old modifiers.
        for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
        {
            const auto id = static_cast<ParamId>(i);
            if (!isPatchDefaultLane(id) || parameterManager.getLaneAmount(id) == 0)
                continue;
            const float base = patchValue(id);
            // A long attack never finishes inside a short gate, so a sustain-0
            // voice would fall silent: Randomize stays at or below the
            // patch's attack or the lane center (~45 ms), whichever is longer.
            const float ceiling = id == ParamId::Attack ? std::max(base, 0.5f) : 1.0f;
            for (uint8_t step = 0; step < getParameterStepCount(id); ++step)
            {
                const float offset = getStepParameterValue(id, step);
                setStepParameterValue(id, step, std::min(offsetAroundBase(base, offset), ceiling));
            }
        }
    }
    if (parameterManager.getLaneAmount(ParamId::Octave) == 0)
        return;
    // Neutral octave across the entire track capacity (64 steps).
    for (uint8_t i = 0; i < SequencerConstants::MAX_STEPS_COUNT; ++i)
        setStepParameterValue(ParamId::Octave, i, 0.5f);
}

float Sequencer::patchValue(ParamId id) const
{
    if (playbackTransform_)
        return playbackTransform_(id, SequencerConstants::LANE_FOLLOWS_PATCH, playbackContext_);
    const auto *definition = parameterDefinition(id);
    return definition ? parameterValueAsFloat(definition->defaultValue) : 0.0f;
}

float Sequencer::getPlaybackValue(ParamId id, uint8_t stepIdx) const
{
    return playbackValue(id, stepIdx);
}

bool Sequencer::followPatch(ParamId id, uint8_t stepIdx)
{
    if (!isPatchDefaultLane(id) || stepIdx >= SequencerConstants::MAX_STEPS_COUNT)
        return false;
    return writeStepValue(id, stepIdx, SequencerConstants::LANE_FOLLOWS_PATCH);
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
