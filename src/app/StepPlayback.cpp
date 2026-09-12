#include "StepPlayback.h"
#include "AppState.h"
#include "ClockService.h"
#include "../sensors/SensorConstants.h"
#include "../sensors/EncoderManager.h"
#include "../ui/UIEventHandler.h"
#include "../midi/MidiManager.h"
#include "../pico2seq-core/scales/scales.h"
#include <algorithm>

namespace
{
constexpr float kGateHighThreshold = 0.5f;
constexpr int kMidiRootNote = 36;
constexpr int kMidiMaximum = 127;
constexpr uint8_t kMidiChannel = 1;
constexpr uint8_t kGateVoiceCount = 2;
constexpr uint8_t kNoMidiVoice = UINT8_MAX;
constexpr int kDistanceDisabled = -1;
}

void updateParametersForStep(uint8_t stepToUpdate)
{
    if (stepToUpdate >= SequencerConstants::MAX_STEPS_COUNT)
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
    if (heldParamId != ParamId::Count)
    {
        // Silent steps keep their pitch while other parameters remain editable.
        if (heldParamId == ParamId::Note)
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
        float valueToSet = mapNormalizedValueToParamRange(heldParamId, normalizedValue);
        activeSeq.setStepParameterValue(heldParamId, stepToUpdate, valueToSet);
        parametersWereUpdated = true;

//                  Keep the two-voice compatibility path; USB MIDI itself is disabled.
//    uint8_t midiVoiceId = (uiState.selectedVoiceIndex == 0) ? 0 : (uiState.selectedVoiceIndex == 1) ? 1 : kNoMidiVoice;
//          if (midiVoiceId != kNoMidiVoice)
//      {
//          midiNoteManager.updateParameterCC(midiVoiceId, heldParamId, valueToSet);
//
    }

    // Provide immediate audio feedback when recording parameters to current step
    if (parametersWereUpdated)
    {
        updateActiveVoiceState(stepToUpdate, activeSeq);
    }
}

void updateVoiceParameters(
    const VoiceState &state,
    bool isVoice2,
    bool updateGate = false,
    volatile bool *gate = nullptr,
    volatile GateTimer *gateTimer = nullptr)
{
    // Voices 0/1 are the only gated (formerly MIDI) voices; isVoice2 selects 1.
    const uint8_t voiceIndex = isVoice2 ? 1 : 0;

    // Handle gate timing and MIDI note events (sequencer playback mode only)
    if (updateGate && gate && gateTimer)
    {
        if (state.isGateHigh)
        {
            // Calculate MIDI note to match audio synthesis approach
            uint8_t noteIndex = static_cast<uint8_t>(std::max(0.0f, std::min(state.noteIndex, static_cast<float>(SCALE_STEPS - 1))));
            int midiNote = scale[currentScale][noteIndex] + kMidiRootNote + static_cast<int>(state.octaveOffset);

            // Always restart the gate timer for gated steps to ensure proper timing
            gateTimer->start(state.gateLengthTicks);

            // Only send MIDI note-on when gate transitions from off to on
            if (!(*gate))
            {
                *gate = true;

                // Clamp MIDI note to valid range (0-127)
                int clampedMidiNote = std::max(0, std::min(midiNote, kMidiMaximum));

                // Use MidiNoteManager for proper note lifecycle management
                midiNoteManager.noteOn(voiceIndex, static_cast<int8_t>(clampedMidiNote),
                                       static_cast<uint8_t>(state.velocityLevel * kMidiMaximum), kMidiChannel, state.gateLengthTicks);
            }
            else
            {
                // Gate is already on - check if note changed and handle retrigger
                int8_t currentActiveNote = midiNoteManager.getActiveNote(voiceIndex);
                if (currentActiveNote != midiNote)
                {
                    // Note changed during gate - retrigger with new note
                    midiNoteManager.noteOn(voiceIndex, static_cast<int8_t>(midiNote),
                                           static_cast<uint8_t>(state.velocityLevel * kMidiMaximum), kMidiChannel, state.gateLengthTicks);
                }
                *gate = true;
            }

            // Update MidiNoteManager gate state
            midiNoteManager.setGateState(voiceIndex, true, state.gateLengthTicks);
        }
        else
        {
            // Step has no gate - turn off immediately
            gateTimer->stop();
            *gate = false;

            // Use MidiNoteManager for proper note-off handling
            midiNoteManager.setGateState(voiceIndex, false);
        }
    }

    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);

    // Voice commits pitch on a high gate so releasing a note preserves its tail.
    voiceManager->updateVoiceState(voiceId, state);
}

void updateVoiceMIDI(
    const VoiceState &state,
    uint8_t voiceIndex,
    bool updateGate = false,
    volatile bool *gate = nullptr,
    volatile GateTimer *gateTimer = nullptr)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES)
    {
        return; // Invalid voice index
    }

    // For voices 0 and 1, reuse existing gate/MIDI logic; for 2/3 skip gates
    bool isVoice2 = (voiceIndex == 1);

    if (updateGate && (voiceIndex < kGateVoiceCount))
    {
        updateVoiceParameters(state, isVoice2, updateGate, gate, gateTimer);
        return;
    }

    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);

    // Push full state to voice (Voice computes frequencies internally on gate HIGH)
    voiceManager->updateVoiceState(voiceId, state);

    // Send MIDI CC only for voices 0 and 1
    if (voiceIndex < kGateVoiceCount)
    {
        uint8_t midiVoiceId = voiceIndex; // 0 or 1
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Filter, state.filterCutoff);
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Attack, state.attackTimeSeconds);
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Decay, state.decayTimeSeconds);
        midiNoteManager.updateParameterCC(midiVoiceId, ParamId::Octave, state.octaveOffset);
    }

}

void updateActiveVoiceState(uint8_t stepIndex, Sequencer &activeSeq)
{
    uint8_t currentSequencerStep = activeSeq.getCurrentStep();

    // Only update currently playing step to avoid audio glitches
    if (stepIndex != currentSequencerStep)
    {
        return;
    }

    // Determine currently selected voice (0-3)
    uint8_t voiceIndex = uiState.selectedVoiceIndex; // 0..3

    if (voiceIndex >= VoiceSystem::MAX_VOICES)
    {
        return; // Invalid voice index
    }

    VoiceState *activeVoiceState = &voiceSystem.getVoiceState(voiceIndex);

    // Update voice state with new step parameters + magnetic encoder modifications
    activeSeq.playStepNow(stepIndex, activeVoiceState);

    // Apply encoder base values for the selected voice (mapping covers all four voices)
    // Sequencer playback composes patch bases before constructing VoiceState.

    // Update synth hardware for immediate audio feedback using the per-voice function
    updateVoiceMIDI(*activeVoiceState, voiceIndex);

}

void processSequencerStep(uint32_t uClockCurrentStep)
{
    if(!isClockRunning || uiState.voiceEditor.active) return;
    g_processedStepCount++;

    VoiceState tempStates[VoiceSystem::MAX_VOICES];
    for(uint8_t i=0;i<VoiceSystem::MAX_VOICES;++i) tempStates[i]=voiceSystem.getVoiceState(i);
    const uint8_t selectedVoice = uiState.selectedVoiceIndex;
    const int handDistance = AppState::performanceInput.distanceAboveMinimumMm;
    // First advance all four voices. Only the selected voice hears the sensor.
    for (uint8_t voice = 0; voice < VoiceSystem::MAX_VOICES; ++voice)
    {
        const int distance = voice == selectedVoice ? handDistance : kDistanceDisabled;
        AppState::sequencers[voice]->setRecordingInput(AppState::performanceInput.recordingValue());
        advanceSequencerStep(*AppState::sequencers[voice], uClockCurrentStep,
                             distance, uiState, &tempStates[voice]);
    }

    // Bases have already been composed by each sequencer's playback transform.

    // Voices 1/2 keep their software gate lifecycle; voices 3/4 use audio only.

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        if (i < kGateVoiceCount)
        {
            updateVoiceMIDI(tempStates[i], i, true,
                                          &voiceSystem.getGate(i),
                                          &voiceSystem.getGateTimer(i));
        }
        else // Voices 2 and 3 are audio only
        {
            updateVoiceMIDI(tempStates[i], i, false);
        }

        // Store state
        voiceSystem.getVoiceState(i) = tempStates[i];
    }
}
