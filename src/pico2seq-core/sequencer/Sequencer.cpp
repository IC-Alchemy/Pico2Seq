// Sequencer: turns lane patterns into sounded steps (notes, tone, articulation).
// One instance per voice; transport runs on Core 0 and hands VoiceStates out.
// Portable C++ — no Arduino/hardware includes here.
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "SequencerDefs.h"
#include "Sequencer.h"

// Live-record distance ceiling; the musical play zone (0-400 mm) maps below it.
constexpr float MAX_SENSOR_DISTANCE_MM = 1100.0f;

constexpr float OCTAVE_LOW_THRESHOLD = 1.0f / 3.0f; // Below this: down an octave (-12)
constexpr float OCTAVE_HIGH_THRESHOLD = 2.0f / 3.0f; // Above this: up an octave (+12)
// Three-zone octave quantize: low/middle/high hand (or knob) region becomes
// a singable -12/0/+12 jump instead of a continuous slide.
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

// int/float/bool lane defaults folded into the float domain tracks store.
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

// Live input (0.0-1.0 hand position) into a lane's musical range. Octave snaps
// to detents; other lanes scale linearly and clamp/round on store.
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

    // setValue() clamps and rounds; the linear map here stays unclamped.
    return minVal + normalizedValue * (maxVal - minVal);
}

Sequencer::Sequencer()
    : Sequencer(0)
{
}

Sequencer::Sequencer(uint8_t channel)
    : running(false), currentStep(0), lastNote(-1), currentNote(-1), noteDurationCounter(0), channel(channel), parameterManager(), previousStepHadSlide(false)
      ,
      envelope()
      ,
      noteDuration()
{
    noteActive = false;
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        currentStepPerParam[i] = 0;
    }

    initializeParameters();
}

bool Sequencer::isNotePlaying() const
{
    // Playing = gate held OR release tail still ringing; either way the voice
    // is not free for a new note.
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
    // Pitch overdubs only onto sounding steps, so rests keep their melody.
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
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        currentStepPerParam[i] = 0;
    }
    running = false;
    previousStepHadSlide = false;
    handleNoteOff(nullptr); // No VoiceState to notify on transport reset
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
    // Patch mode stores neutral modifiers, not raw defaults (see header).
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
        // Widen first: setValue wraps at the active length, so neutralizing a
        // short track would corrupt its head with tail values. Every slot is
        // overwritten right after, then lengths shrink back to 16.
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
        // Boot defaults across the full 64-slot capacity, not just the loop.
        initializeParameters();
    }
    // Back to 16-step loops everywhere; ends any ringing note like reset().
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

    // Bar cursor follows the Gate lane; narrowing only after modulo avoids
    // aliasing every 256 steps onto position 0.
    uint8_t sequenceLength = getParameterStepCount(ParamId::Gate);
    if (sequenceLength > 0)
    {
        currentStep = static_cast<uint8_t>(current_uclock_step % sequenceLength);
    }
    else
    {
        currentStep = 0; // No Gate length yet; park the cursor at step 0
    }

    // Each lane wraps on its own length: different lengths phase into polyrhythm.
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i)
    {
        ParamId paramId = static_cast<ParamId>(i);
        uint8_t paramStepCount = getParameterStepCount(paramId);

        if (paramStepCount > 0)
        {
            currentStepPerParam[i] = static_cast<uint8_t>(current_uclock_step % paramStepCount);
        }
        else
        {
            currentStepPerParam[i] = 0; // Unset length; park at step 0
        }
    }

    // Live overdub first (skipped in step-edit mode so the hand never fights a selection).
    if (mm_distance >= 0 && current_selected_step_for_edit == -1)
    {
        // Hand position (or calibrated knob input) as 0.0-1.0 for the lane mappers.
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
            // Slide is step-pressed only: live overdub here used to erase slides.
        };

        for (const auto &pb : paramButtons)
        {
            if (pb.held)
            {
                // Overdub at that lane's own cursor (Note still skips resting steps).
                recordLiveValue(pb.id, mapNormalizedValueToParamRange(pb.id, normalizedDistance));
            }
        }
    }

    // Sound the combined cursors (UINT8_MAX = per-lane positions), including
    // anything just overdubbed, so the player hears the edit immediately.
    recordingInput_ = -1.0f;
    processStep(UINT8_MAX, voiceState);
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
        // Scale degree + octave transpose, clamped to the MIDI range.
        int rawNote = static_cast<int>(noteVal) + octaveOffset;
        int finalNote = std::max(0, std::min(rawNote, 127));

        // Gated step: fresh attack, or a legato slide from the ringing note.
        // A slide with no active note still attacks so it is never silent.
        if (!slideVal || !noteActive)
        {
            // New articulation: restart the envelope for a crisp front.
            if (voiceState)
            {
                voiceState->shouldRetrigger = true;
            }

            startNote(static_cast<uint8_t>(finalNote), static_cast<uint8_t>(velocityVal * 127.0f),
                      noteDurationTicks);
        }
        else
        {
            // Legato: bend pitch without restarting the envelope, but re-arm
            // the hold time for this step's gate length.
            currentNote = static_cast<int8_t>(finalNote);
            noteActive = true;
            noteDuration.start(noteDurationTicks);
        }
    }
    else
    {
        // Rest: release, unless the previous step's slide is still carrying
        // the phrase through it.
        if (!previousStepHadSlide)
        {
            handleNoteOff(voiceState);
        }
    }

    // Previews may pass null; sounding steps need a VoiceState to write into.
    if (voiceState)
    {
        // Tone always follows the step; pitch only on gated steps so a resting
        // step never steals the ringing note — it fades on its own pitch.
        voiceState->filterCutoff = filterVal;
        voiceState->attackTimeSeconds = attackVal;
        voiceState->decayTimeSeconds = decayVal;
        voiceState->sustainLevel = sustainVal;
        voiceState->releaseTimeSeconds = releaseVal;
        voiceState->velocityLevel = velocityVal;
        voiceState->isGateHigh = gateOn;
        voiceState->hasSlide = slideVal;
        voiceState->gateLengthTicks = noteDurationTicks;

        // Gate off: keep the old pitch so the tail rings instead of jumping.
        if (gateOn)
        {
            voiceState->noteIndex = noteVal; // Raw scale degree; audio quantizes it
            voiceState->octaveOffset = octaveOffset;
        }
        // When gate is LOW, preserve the previous note and octave values
    }

    // Remember slide-through-rests for the next step.
    previousStepHadSlide = slideVal && gateOn;
}

void Sequencer::startNote(uint8_t note, uint8_t velocity, uint16_t duration)
{
    currentNote = static_cast<int8_t>(note);
    noteActive = true;
    lastNote = currentNote;

    // Hold the note for its gate length with the envelope struck.
    noteDuration.start(duration);
    triggerEnvelope();
}

void Sequencer::handleNoteOff(VoiceState *voiceState)
{
    if (noteActive)
    {
        // Optional external note-off routing (legacy MIDI hook).
        if (midiNoteOffCallback)
        {
            midiNoteOffCallback(static_cast<uint8_t>(currentNote), channel);
        }

        currentNote = -1;
        noteActive = false;
        releaseEnvelope();
        noteDuration.reset();

        // Signal the audio engine: gate down, no pending retrigger.
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
            // Hold expired mid-step: release the note and report the edge.
            handleNoteOff(voiceState);
            return true;
        }
    }
    return false;
}

void Sequencer::playStepNow(uint8_t stepIdx, VoiceState *voiceState)
{
    // Audition one stored step exactly as the transport would play it.
    processStep(stepIdx, voiceState);
}

void Sequencer::previewActiveStep(VoiceState *voiceState)
{
    // Audition the currently sounding lane combination (per-lane cursors).
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
    // Pitch tracks only a held gate, matching processStep(): released tails
    // keep their pitch instead of jumping to the next step.
    if (voiceState->isGateHigh)
    {
        voiceState->noteIndex = values.noteIndex;
        voiceState->octaveOffset = values.octaveOffset;
    }
    voiceState->shouldRetrigger = false;
}

void Sequencer::toggleStep(uint8_t stepIdx)
{
    // Flip this step between sounding and resting.
    float gate = getStepParameterValue(ParamId::Gate, stepIdx);
    setStepParameterValue(ParamId::Gate, stepIdx, (gate > 0.5f) ? 0.0f : 1.0f); // Toggle lanes snap at 0.5
}

namespace
{
// One decode path for stored, previewed, and played steps: the caller picks
// the lane values (cursor vs. index, raw vs. patch-mapped), decodeStep only
// converts. Template (not std::function) keeps the hot path allocation-free.
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

    // Octave lane stores semitones; encode back to its 0.0-1.0 detents.
    const float octaveVal = std::clamp(static_cast<float>(step.octaveOffset) / 48.0f + 0.5f, 0.0f, 1.0f);
    setStepParameterValue(ParamId::Octave, stepIdx, octaveVal);

    // Ticks back to a 0.001-1.0 step fraction; shortest still sounds (never 0).
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
        // Humanize draws offsets around 0.5; in patch mode store what the step
        // actually plays — the offset spread around the patch base.
        for (uint8_t i = 0; i < PARAM_ID_COUNT; ++i)
        {
            const auto id = static_cast<ParamId>(i);
            if (!isPatchDefaultLane(id) || parameterManager.getLaneAmount(id) == 0)
                continue;
            const float base = patchValue(id);
            // Cap attacks: a longer-than-gate attack on a sustain-0 voice would
            // go silent, so stay at/below the patch attack or lane center.
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
    // Humanize leaves octave centered (concert pitch) across all 64 slots.
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
