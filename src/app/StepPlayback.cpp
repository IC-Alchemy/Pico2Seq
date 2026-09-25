#include "StepPlayback.h"
#include "AppState.h"
#include "ClockService.h"
#include "../sensors/SensorConstants.h"
#include "../sensors/EncoderManager.h"
#include "../ui/UIEventHandler.h"
#include "VoicePlayback.h"
#include <algorithm>

// Clock-step fan-out + live hand/fader recording (see header). All Core 0 thread
// context; note duration itself is ticked separately via tickSequencerVoices().

namespace
{
constexpr int kDistanceDisabled = -1;
}

bool recordParameter(ParamId id, float normalizedValue)
{
    if (uiState.voiceEditor.active || uiState.controlsWaitRelease || id >= ParamId::Count)
        return false;

    // Keep the legacy voice-4 fallback for an invalid UI selection.
    Sequencer &activeSeq = AppState::sequencerView.clamped(uiState.selectedVoiceIndex);
    const float value = mapNormalizedValueToParamRange(id, normalizedValue);
    const int selected = uiState.selectedStepForEdit;
    const bool stepEdit = selected >= 0 && selected < SequencerConstants::MAX_STEPS_COUNT;
    // Step Edit targets the selected step; live recording targets the playing step
    // (between steps too, so the note tracks the hand). Note stays gate-protected.
    const bool changed = stepEdit ? activeSeq.editStepValue(id, static_cast<uint8_t>(selected), value)
                                  : activeSeq.recordLiveValue(id, value);
    // Runs every 1 ms pass: only a real change (post-clamp/rounding) retunes audio.
    if (changed)
        updateActiveVoiceState(stepEdit ? static_cast<uint8_t>(selected) : UINT8_MAX, activeSeq);
    return changed;
}

bool resetStepToPatch(ParamId id)
{
    const int selected = uiState.selectedStepForEdit;
    if (uiState.voiceEditor.active || uiState.controlsWaitRelease || selected < 0 ||
        selected >= SequencerConstants::MAX_STEPS_COUNT)
        return false;
    Sequencer &activeSeq = AppState::sequencerView.clamped(uiState.selectedVoiceIndex);
    const bool changed = activeSeq.followPatch(id, static_cast<uint8_t>(selected));
    if (changed)
        updateActiveVoiceState(static_cast<uint8_t>(selected), activeSeq);
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

    // Refresh in place: re-running the step would retrigger the envelope every
    // pass (oscillators choked, waveguides re-plucked). Unplayed steps sound on arrival.
    VoiceState &activeVoiceState = voiceSystem.getVoiceState(voiceIndex);
    if (isClockRunning)
    {
        // A Step Edit write names the selected step explicitly. Refreshing the
        // lane cursors instead would display one step while retuning another,
        // which made Filter/Release edits appear to do nothing.
        activeSeq.refreshVoiceParameters(&activeVoiceState, stepIndex);
    }
    else
    {
        const Step values = activeSeq.getPlaybackStep(stepIndex < SequencerConstants::MAX_STEPS_COUNT ? stepIndex : UINT8_MAX);
        activeVoiceState.velocityLevel = values.velocityLevel;
        activeVoiceState.filterCutoff = values.filterCutoff;
        activeVoiceState.attackTimeSeconds = values.attackTimeSeconds;
        activeVoiceState.decayTimeSeconds = values.decayTimeSeconds;
        activeVoiceState.sustainLevel = values.sustainLevel;
        activeVoiceState.releaseTimeSeconds = values.releaseTimeSeconds;
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
    // With no hand present, recording pauses and steps hold their values.
    const int handDistance = AppState::performanceInput.handPresent
                                 ? AppState::performanceInput.distanceAboveMinimumMm : kDistanceDisabled;
    // Advance all four voices first; only the selected voice hears the sensor.
    for (uint8_t voice = 0; voice < VoiceSystem::MAX_VOICES; ++voice)
    {
        const int distance = voice == selectedVoice ? handDistance : kDistanceDisabled;
        AppState::sequencers[voice]->setRecordingInput(AppState::performanceInput.recordingValue());
        advanceSequencerStep(*AppState::sequencers[voice], uClockCurrentStep,
                             distance, uiState, &tempStates[voice]);
    }

    // Playback transforms already folded patch bases into these states.

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
        publishVoiceState(i, tempStates[i]);
}
