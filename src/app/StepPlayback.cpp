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

bool recordParameter(ParamId id, float normalizedValue)
{
    if (uiState.voiceEditor.active || uiState.controlsWaitRelease || id >= ParamId::Count)
        return false;

    // Retain the existing fallback to voice 4 for an invalid UI selection.
    Sequencer &activeSeq = AppState::sequencerView.clamped(uiState.selectedVoiceIndex);
    const float value = mapNormalizedValueToParamRange(id, normalizedValue);
    const int selected = uiState.selectedStepForEdit;
    const bool stepEdit = selected >= 0 && selected < SequencerConstants::MAX_STEPS_COUNT;
    // Step Edit writes the selected step. Otherwise this is live recording:
    // the lane's playing step, between clock steps as well as on them, so the
    // sounding note follows the hand or fader. Both keep Note gate-protected.
    const bool changed = stepEdit ? activeSeq.editStepValue(id, static_cast<uint8_t>(selected), value)
                                  : activeSeq.recordLiveValue(id, value);
    // This runs every control pass (1 ms). Only an actual change (after
    // clamping and note rounding) refreshes the voice.
    if (changed)
        updateActiveVoiceState(stepEdit ? static_cast<uint8_t>(selected) : UINT8_MAX, activeSeq);
    return changed;
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
