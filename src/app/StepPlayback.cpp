#include "StepPlayback.h"
#include "AppState.h"
#include "ClockService.h"
#include "../sensors/SensorConstants.h"
#include "../ui/ParameterEditing.h"
#include "VoicePlayback.h"
#include <algorithm>

void updateParametersForStep(uint8_t stepToUpdate)
{
    if (stepToUpdate >= SequencerConstants::MAX_STEPS_COUNT)
        return;
    if (uiState.selectedVoiceIndex >= VoiceSystem::MAX_VOICES) return;
    Sequencer &sequence = *AppState::sequencers[uiState.selectedVoiceIndex];
    const auto result = ParameterEditing::record(uiState, sequence, stepToUpdate,
        AppState::performanceInput.recordingValue(), AppState::performanceInput.handPresent);
    if (result.changed()) updateActiveVoiceState(stepToUpdate, sequence);
}

void updateParametersForStepNormalized(uint8_t stepToUpdate, float normalizedValue)
{
    if (uiState.selectedVoiceIndex >= VoiceSystem::MAX_VOICES) return;
    Sequencer &sequence = *AppState::sequencers[uiState.selectedVoiceIndex];
    const auto result = ParameterEditing::record(uiState, sequence, stepToUpdate, normalizedValue, true);
    if (result.changed()) updateActiveVoiceState(stepToUpdate, sequence);
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
    activeSeq.refreshVoiceParameters(&activeVoiceState,
        isClockRunning ? UINT8_MAX : stepIndex, !isClockRunning);
    publishVoiceState(voiceIndex, activeVoiceState);
}

void processSequencerStep(uint32_t uClockCurrentStep)
{
    if(!isClockRunning || uiState.voiceEditor.active) return;
    g_processedStepCount++;

    VoiceState tempStates[VoiceSystem::MAX_VOICES];
    for(uint8_t i=0;i<VoiceSystem::MAX_VOICES;++i) tempStates[i]=voiceSystem.getVoiceState(i);
    for (uint8_t voice = 0; voice < VoiceSystem::MAX_VOICES; ++voice)
        ParameterEditing::advance(*AppState::sequencers[voice], voice, uClockCurrentStep,
            uiState, AppState::performanceInput.recordingValue(),
            AppState::performanceInput.handPresent, tempStates[voice]);

    // Bases have already been composed by each sequencer's playback transform.

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
        publishVoiceState(i, tempStates[i]);
}
