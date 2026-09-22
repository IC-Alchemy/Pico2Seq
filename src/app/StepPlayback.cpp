#include "StepPlayback.h"
#include "AppState.h"
#include "ClockService.h"
#include "../sensors/SensorConstants.h"
#include "../sensors/EncoderManager.h"
#include "../ui/UIEventHandler.h"
#include "../voice/VoiceEditParameters.h"
#include "VoicePlayback.h"
#include <algorithm>
#include <Arduino.h>

// Clock-step fan-out + live hand/fader recording (see header). All Core 0 thread
// context; note duration itself is ticked separately via tickSequencerVoices().

namespace
{
constexpr int kDistanceDisabled = -1;
bool g_sequencerTraceEnabled = false;

void printSequencerTrace(uint32_t clockStep, uint8_t voice, const Sequencer &seq,
                         const VoiceState &published)
{
    const uint8_t voiceId = voiceSystem.getVoiceId(voice);
    const VoiceConfig *config = voiceManager ? voiceManager->getVoiceConfig(voiceId) : nullptr;
    const uint8_t filterStep = seq.getCurrentStepForParameter(ParamId::Filter);
    const uint8_t attackStep = seq.getCurrentStepForParameter(ParamId::Attack);
    const uint8_t decayStep = seq.getCurrentStepForParameter(ParamId::Decay);
    const float filterRaw = seq.getStepParameterValue(ParamId::Filter, filterStep);
    const float attackRaw = seq.getStepParameterValue(ParamId::Attack, attackStep);
    const float decayRaw = seq.getStepParameterValue(ParamId::Decay, decayStep);
    const float filterComposed = config ? VoiceEdit::composeLane(ParamId::Filter, filterRaw, config) : filterRaw;
    const float attackComposed = config ? VoiceEdit::composeLane(ParamId::Attack, attackRaw, config) : attackRaw;
    const float decayComposed = config ? VoiceEdit::composeLane(ParamId::Decay, decayRaw, config) : decayRaw;
    Serial.printf("[SEQTRACE] clk=%lu v=%u id=%u gateStep=%u gate=%u note=%.2f "
                  "F{s=%u raw=%.5f cmp=%.5f pub=%.5f} "
                  "A{s=%u raw=%.5f cmp=%.5f pub=%.5f} "
                  "D{s=%u raw=%.5f cmp=%.5f pub=%.5f}",
                  static_cast<unsigned long>(clockStep), static_cast<unsigned>(voice + 1),
                  static_cast<unsigned>(voiceId), static_cast<unsigned>(seq.getCurrentStep()),
                  published.isGateHigh ? 1u : 0u, published.noteIndex,
                  static_cast<unsigned>(filterStep), filterRaw, filterComposed, published.filterCutoff,
                  static_cast<unsigned>(attackStep), attackRaw, attackComposed, published.attackTimeSeconds,
                  static_cast<unsigned>(decayStep), decayRaw, decayComposed, published.decayTimeSeconds);
    if (config)
    {
        const float attackBase = VoiceEdit::composeLane(
            ParamId::Attack, SequencerConstants::LANE_FOLLOWS_PATCH, config);
        const float decayBase = VoiceEdit::composeLane(
            ParamId::Decay, SequencerConstants::LANE_FOLLOWS_PATCH, config);
        Serial.printf(" cfg{engine=%u paramSet=%u patch=%u hasFilter=%u hasEnv=%u "
                      "baseF=%.5f baseA=%.5f baseD=%.5f attackSec=%.5f decaySec=%.5f "
                      "envOct=%.3f envRest=%.3f}",
                      static_cast<unsigned>(config->engine), static_cast<unsigned>(config->paramSet),
                      config->usePatchBases ? 1u : 0u, config->hasFilter ? 1u : 0u,
                      config->hasEnvelope ? 1u : 0u, config->filterCutoffBase,
                      attackBase, decayBase, config->defaultAttack, config->defaultDecay,
                      config->filterEnvelopeOctaves,
                      config->filterEnvelopeRest);
    }
    Serial.println();
}
}

bool sequencerTraceEnabled() noexcept { return g_sequencerTraceEnabled; }
void setSequencerTraceEnabled(bool enabled) noexcept { g_sequencerTraceEnabled = enabled; }

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
        activeSeq.refreshVoiceParameters(&activeVoiceState);
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

    if (g_sequencerTraceEnabled)
        for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
            printSequencerTrace(uClockCurrentStep, i, *AppState::sequencers[i], tempStates[i]);
}
