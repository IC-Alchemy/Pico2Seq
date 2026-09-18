#include "StepPlayback.h"
#include "AppState.h"
#include "ClockService.h"
#include "../sensors/SensorConstants.h"
#include "../sensors/EncoderManager.h"
#include "../ui/UIEventHandler.h"
#include "VoicePlayback.h"
#include <algorithm>

namespace
{
constexpr float kGateHighThreshold = 0.5f;
constexpr int kDistanceDisabled = -1;
}

void updateParametersForStep(uint8_t stepToUpdate)
{
    if (stepToUpdate >= SequencerConstants::MAX_STEPS_COUNT)
        return;
    // No hand over the sensor: keep what the step already holds.
    if (!AppState::performanceInput.handPresent)
        return;

    updateParametersForStepNormalized(stepToUpdate, AppState::performanceInput.recordingValue());
}

void updateParametersForStepNormalized(uint8_t stepToUpdate, float normalizedValue)
{
    if(uiState.voiceEditor.active || uiState.controlsWaitRelease) return;
    if (stepToUpdate >= SequencerConstants::MAX_STEPS_COUNT)
        return;

    // Retain the existing fallback to voice 4 for an invalid UI selection.
    const uint8_t sequenceIndex = uiState.selectedVoiceIndex < VoiceSystem::MAX_VOICES
                                      ? uiState.selectedVoiceIndex : VoiceSystem::MAX_VOICES - 1;
    Sequencer &activeSeq = *AppState::sequencers[sequenceIndex];

    bool parametersWereUpdated = false;
    const ParamId heldParamId = getHeldParameterParamId(uiState);
    const ParamId paramToEdit = (heldParamId != ParamId::Count) ? heldParamId : uiState.currentEditParameter;
    if (paramToEdit != ParamId::Count)
    {
        // Silent steps keep their pitch while other parameters remain editable.
        if (paramToEdit == ParamId::Note)
        {
            float gateValue = activeSeq.getStepParameterValue(ParamId::Gate, stepToUpdate);
            if (gateValue <= kGateHighThreshold)
            {
                // Skip Note parameter editing on steps with LOW gates
                // This protects steps from note frequency changes during programming/editing
                return;
            }
        }

        // Use the helper function to do the scaling correctly for any parameter.
        float valueToSet = mapNormalizedValueToParamRange(paramToEdit, normalizedValue);
        const float previousValue = activeSeq.getStepParameterValue(paramToEdit, stepToUpdate);
        activeSeq.setStepParameterValue(paramToEdit, stepToUpdate, valueToSet);
        // This runs every control pass (1 ms) while a step is in edit. Only an
        // actual change (after clamping and note rounding) is previewed, so a
        // steady hand does not retrigger the step each pass.
        parametersWereUpdated = activeSeq.getStepParameterValue(paramToEdit, stepToUpdate) != previousValue;

    }

    // Provide immediate audio feedback when recording parameters to current step
    if (parametersWereUpdated)
    {
        updateActiveVoiceState(stepToUpdate, activeSeq);
    }
}

void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq)
{
    uint8_t voiceIndex = VoiceSystem::MAX_VOICES;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
    {
        if (AppState::sequencers[i] == &activeSeq)
        {
            voiceIndex = i;
            break;
        }
    }
    if (voiceIndex >= VoiceSystem::MAX_VOICES)
    {
        return;
    }

    // UI edits arrive every control pass (lidar, faders, encoder). Re-running
    // the step here retriggered the envelope on each one, which kept
    // oscillator voices near silent while waveguides kept re-plucking.
    // Refresh the sounding voice in place instead; an edit to a step that is
    // not playing is heard when its cursor comes round.
    VoiceState &activeVoiceState = voiceSystem.getVoiceState(voiceIndex);
    if (isClockRunning)
    {
        activeSeq.refreshVoiceParameters(&activeVoiceState);
    }
    else
    {
        const Step values = activeSeq.getPlaybackStep(stepIndex < SequencerConstants::MAX_STEPS_COUNT ? stepIndex : UINT8_MAX);
        activeVoiceState.velocityLevel = values.velocityLevel;
        activeVoiceState.filterCutoff = values.filterCutoff;
        activeVoiceState.attackTimeSeconds = values.attackTimeSeconds;
        activeVoiceState.decayTimeSeconds = values.decayTimeSeconds;
        activeVoiceState.noteIndex = values.noteIndex;
        activeVoiceState.octaveOffset = values.octaveOffset;
        activeVoiceState.shouldRetrigger = false;
    }
    publishVoiceState(voiceIndex, activeVoiceState);
}

void processSequencerStep(uint32_t uClockCurrentStep)
{
    if(!isClockRunning || uiState.voiceEditor.active) return;
    g_processedStepCount++;

    VoiceState tempStates[VoiceSystem::MAX_VOICES];
    for(uint8_t i=0;i<VoiceSystem::MAX_VOICES;++i) tempStates[i]=voiceSystem.getVoiceState(i);
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
