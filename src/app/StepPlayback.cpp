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
    uint8_t voiceIndex,
    bool updateGate = false,
    volatile bool *gate = nullptr,
    volatile GateTimer *gateTimer = nullptr)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES)
    {
        return;
    }

    // Handle gate timing and MIDI note events (sequencer playback mode only)
    if (updateGate && gate && gateTimer)
    {
        if (state.isGateHigh)
        {
            // Always restart the gate timer for gated steps to ensure proper timing
            gateTimer->start(state.gateLengthTicks);

            if (voiceIndex < kGateVoiceCount)
            {
                // Calculate MIDI note to match audio synthesis approach
                uint8_t noteIndex = static_cast<uint8_t>(std::max(0.0f, std::min(state.noteIndex, static_cast<float>(SCALE_STEPS - 1))));
                int midiNote = scale[currentScale][noteIndex] + kMidiRootNote + static_cast<int>(state.octaveOffset);

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
                    int clampedMidiNote = std::max(0, std::min(midiNote, kMidiMaximum));
                    if (currentActiveNote != clampedMidiNote)
                    {
                        // Note changed during gate - retrigger with new note
                        midiNoteManager.noteOn(voiceIndex, static_cast<int8_t>(clampedMidiNote),
                                               static_cast<uint8_t>(state.velocityLevel * kMidiMaximum), kMidiChannel, state.gateLengthTicks);
                    }
                    *gate = true;
                }

                // Update MidiNoteManager gate state
                midiNoteManager.setGateState(voiceIndex, true, state.gateLengthTicks);
            }
            else
            {
                *gate = true;
            }
        }
        else
        {
            // Step has no gate - turn off immediately
            gateTimer->stop();
            *gate = false;

            if (voiceIndex < kGateVoiceCount)
            {
                // Use MidiNoteManager for proper note-off handling
                midiNoteManager.setGateState(voiceIndex, false);
            }
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

    if (updateGate)
    {
        updateVoiceParameters(state, voiceIndex, updateGate, gate, gateTimer);
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
    (void)stepIndex; // the playing cursors decide what is heard

    // A stopped transport is muted, and the note lifecycle belongs to the
    // clock: editing while stopped just stores the value.
    if (!isClockRunning)
    {
        return;
    }

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
    activeSeq.refreshVoiceParameters(&activeVoiceState);
    updateVoiceMIDI(activeVoiceState, voiceIndex);
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

    // Voices 0-3 all have software gate and timer tracking; 0-1 retain MIDI bookkeeping.
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        updateVoiceMIDI(tempStates[i], i, true,
                        &voiceSystem.getGate(i),
                        &voiceSystem.getGateTimer(i));

        // Store state. The retrigger event belongs to the push above; a
        // stored copy that kept it would restart the envelope whenever this
        // state is sent again (live edits, gate-length expiry).
        voiceSystem.getVoiceState(i) = tempStates[i];
        voiceSystem.getVoiceState(i).shouldRetrigger = false;
    }
}
