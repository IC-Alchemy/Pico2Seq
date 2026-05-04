#include "Sequencer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float OCTAVE_LOW_THRESHOLD = 0.15f;
constexpr float OCTAVE_HIGH_THRESHOLD = 0.4f;
constexpr int8_t OCTAVE_OFFSET_DOWN = -12;
constexpr int8_t OCTAVE_OFFSET_UP = 12;
constexpr int8_t OCTAVE_OFFSET_NONE = 0;

int8_t mapFloatToOctaveOffset(float octaveValue)
{
    if (octaveValue < OCTAVE_LOW_THRESHOLD) {
        return OCTAVE_OFFSET_DOWN;
    }
    if (octaveValue > OCTAVE_HIGH_THRESHOLD) {
        return OCTAVE_OFFSET_UP;
    }
    return OCTAVE_OFFSET_NONE;
}

float mapNormalizedValueToParamRange(ParamId id, float normalizedValue)
{
    const auto &def = CORE_PARAMETERS[static_cast<size_t>(id)];

    auto getFloat = [](const ParameterValueType &v) -> float {
        return std::visit(
            [](auto &&arg) -> float {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) return arg;
                if constexpr (std::is_same_v<T, int>) return static_cast<float>(arg);
                if constexpr (std::is_same_v<T, bool>) return arg ? 1.0f : 0.0f;
                return 0.0f;
            },
            v);
    };

    const float clamped = std::max(0.0f, std::min(normalizedValue, 1.0f));
    const float minVal = getFloat(def.minValue);
    const float maxVal = getFloat(def.maxValue);
    return minVal + clamped * (maxVal - minVal);
}

void Sequencer::commonInit()
{
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i) {
        currentStepPerParam[i] = 0;
    }
    initializeParameters();
}

Sequencer::Sequencer()
{
    commonInit();
}

Sequencer::Sequencer(uint8_t channel) : channel(channel)
{
    commonInit();
}

bool Sequencer::isNotePlaying() const
{
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
    if (id == ParamId::Note) {
        const float gateValue = getStepParameterValue(ParamId::Gate, stepIdx);
        if (gateValue <= 0.5f) {
            return;
        }
    }

    parameterManager.setValue(id, stepIdx, value);
}

void Sequencer::reset()
{
    currentStep = 0;
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i) {
        currentStepPerParam[i] = 0;
    }
    running = false;
    previousStepHadSlide = false;
    handleNoteOff(nullptr);
}

uint8_t Sequencer::getCurrentStepForParameter(ParamId paramId) const
{
    return currentStepPerParam[static_cast<size_t>(paramId)];
}

void Sequencer::resetAllSteps()
{
    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i) {
        ParamId currentId = static_cast<ParamId>(i);
        float defaultValue = std::visit(
            [](auto &&arg) -> float {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) return arg;
                if constexpr (std::is_same_v<T, int>) return static_cast<float>(arg);
                if constexpr (std::is_same_v<T, bool>) return arg ? 1.0f : 0.0f;
                return 0.0f;
            },
            CORE_PARAMETERS[i].defaultValue);

        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
            parameterManager.setValue(currentId, step, defaultValue);
        }
    }
}

void Sequencer::advanceStep(uint8_t current_uclock_step, const ParameterInputState &input, VoiceState *voiceState)
{
    if (!running) {
        return;
    }

    const uint8_t sequenceLength = getParameterStepCount(ParamId::Gate);
    currentStep = sequenceLength > 0 ? current_uclock_step % sequenceLength : 0;

    for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i) {
        const ParamId paramId = static_cast<ParamId>(i);
        const uint8_t paramStepCount = getParameterStepCount(paramId);
        currentStepPerParam[i] = paramStepCount > 0 ? current_uclock_step % paramStepCount : 0;
    }

    if (input.normalizedValue >= 0.0f && input.selectedStepForEdit == -1) {
        for (size_t i = 0; i < static_cast<size_t>(ParamId::Count); ++i) {
            if (!input.held[i]) {
                continue;
            }

            const ParamId paramId = static_cast<ParamId>(i);
            const uint8_t paramStepIdx = currentStepPerParam[i];
            if (paramId == ParamId::Note &&
                getStepParameterValue(ParamId::Gate, paramStepIdx) <= 0.5f) {
                continue;
            }

            const float value = mapNormalizedValueToParamRange(paramId, input.normalizedValue);
            setStepParameterValue(paramId, paramStepIdx, value);
        }
    }

    processStep(UINT8_MAX, voiceState);
}

void Sequencer::processStep(uint8_t stepIdx, VoiceState *voiceState)
{
    const bool usePerParameterIndices = (stepIdx == UINT8_MAX);

    if (voiceState) {
        voiceState->shouldRetrigger = false;
    }

    auto paramStep = [&](ParamId id) {
        return usePerParameterIndices ? currentStepPerParam[static_cast<size_t>(id)] : stepIdx;
    };

    const float gateOn = getStepParameterValue(ParamId::Gate, paramStep(ParamId::Gate));
    const float filterVal = getStepParameterValue(ParamId::Filter, paramStep(ParamId::Filter));
    const float attackVal = getStepParameterValue(ParamId::Attack, paramStep(ParamId::Attack));
    const float decayVal = getStepParameterValue(ParamId::Decay, paramStep(ParamId::Decay));
    const float noteVal = getStepParameterValue(ParamId::Note, paramStep(ParamId::Note));
    const float velocityVal = getStepParameterValue(ParamId::Velocity, paramStep(ParamId::Velocity));
    const float octaveFloat = getStepParameterValue(ParamId::Octave, paramStep(ParamId::Octave));
    const float slideVal = getStepParameterValue(ParamId::Slide, paramStep(ParamId::Slide));
    const float gateLengthProportion =
        getStepParameterValue(ParamId::GateLength, paramStep(ParamId::GateLength));

    const uint16_t noteDurationTicks = static_cast<uint16_t>(
        std::max(1.0f, gateLengthProportion * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
    const int8_t octaveOffset = mapFloatToOctaveOffset(octaveFloat);

    if (gateOn) {
        const int finalNote = static_cast<int>(noteVal) + octaveOffset;

        if (!slideVal) {
            if (voiceState) {
                voiceState->shouldRetrigger = true;
            }
            startNote(static_cast<uint8_t>(std::max(0, finalNote)),
                      static_cast<uint8_t>(velocityVal * 127.0f),
                      noteDurationTicks);
        } else {
            currentNote = static_cast<int8_t>(finalNote);
            noteDuration.start(noteDurationTicks);
        }
    } else if (!previousStepHadSlide) {
        handleNoteOff(voiceState);
    }

    if (voiceState) {
        voiceState->filterCutoff = filterVal;
        voiceState->attackTimeSeconds = attackVal;
        voiceState->decayTimeSeconds = decayVal;
        voiceState->velocityLevel = velocityVal;
        voiceState->isGateHigh = gateOn;
        voiceState->hasSlide = slideVal;
        voiceState->gateLengthTicks = noteDurationTicks;

        if (gateOn) {
            voiceState->noteIndex = noteVal;
            voiceState->octaveOffset = octaveOffset;
        }
    }

    previousStepHadSlide = slideVal && gateOn;
}

void Sequencer::startNote(uint8_t note, uint8_t velocity, uint16_t duration)
{
    (void)velocity;
    currentNote = static_cast<int8_t>(note);
    lastNote = currentNote;
    noteDuration.start(duration);
    triggerEnvelope();
}

void Sequencer::handleNoteOff(VoiceState *voiceState)
{
    if (currentNote < 0) {
        return;
    }

    if (midiNoteOffCallback) {
        midiNoteOffCallback(static_cast<uint8_t>(currentNote), channel);
    }

    currentNote = -1;
    releaseEnvelope();
    noteDuration.reset();

    if (voiceState) {
        voiceState->isGateHigh = false;
        voiceState->shouldRetrigger = false;
    }
}

void Sequencer::tickNoteDuration(VoiceState *voiceState)
{
    if (!noteDuration.isActive()) {
        return;
    }

    noteDuration.tick();
    if (!noteDuration.isActive()) {
        handleNoteOff(voiceState);
    }
}

void Sequencer::playStepNow(uint8_t stepIdx, VoiceState *voiceState)
{
    processStep(stepIdx, voiceState);
}

void Sequencer::toggleStep(uint8_t stepIdx)
{
    const float gate = getStepParameterValue(ParamId::Gate, stepIdx);
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
    s.gateLengthTicks = static_cast<uint16_t>(
        std::max(1.0f, gateLengthProportion * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
    return s;
}

void Sequencer::randomizeParameters()
{
    parameterManager.randomizeParameters();
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
