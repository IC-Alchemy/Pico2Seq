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

    // The focused parameter (newest physical hold, then the latch) decides
    // the lane; with nothing held or latched, the step's toggled edit
    // parameter takes over.
    const ParamId paramToEdit = ControlSurface::stepEditParameter(
        focusedParameterId(uiState), uiState.currentEditParameter,
        EncoderParameterMode::COUNT);
    if (paramToEdit == ParamId::Count)
        return;

    // A manual encoder/fader edit owns this target until the hand leaves the
    // sensor window; a stationary valid reading must not replace it.
    if (uiState.stepEditOwner.suppressesLidar(sequenceIndex,
                                              static_cast<uint8_t>(paramToEdit),
                                              stepToUpdate))
        return;

    // One shared write: gate rule for Note recording sits with the clamping
    // and change detection, and only an actual change is previewed.
    const StepWriteResult written = activeSeq.writeStepParameter(
        paramToEdit, stepToUpdate, normalizedValue,
        StepWriteDomain::Normalized01, true, NoteGateRule::AtStep);
    if (written.status == StepWriteStatus::Changed)
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
        activeSeq.refreshVoiceParametersAt(
            stepIndex < SequencerConstants::MAX_STEPS_COUNT ? stepIndex : UINT8_MAX,
            &activeVoiceState);
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
