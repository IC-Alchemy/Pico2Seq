#include "MidiManager.h"
#include "../scales/scales.h"  // ADD THIS LINE
#include "../sequencer/Sequencer.h"
#include "../sequencer/SequencerDefs.h" // For VoiceState and GateTimer definitions
#include <algorithm> // For std::max, std::min, std::abs
#include <cmath> // For mathematical functions

// External references
extern midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi;
extern Sequencer seq1, seq2;
extern VoiceState voiceState1, voiceState2;
extern GateTimer gateTimer1, gateTimer2;
extern volatile bool GATE1, GATE2;

// Global MIDI note manager instance
MidiNoteManager midiNoteManager;

// =======================
//   MIDI NOTE MANAGER IMPLEMENTATION
// =======================

MidiNoteManager::MidiNoteManager() {
    // Initialize trackers
    voice1Tracker.reset();
    voice2Tracker.reset();
}

MidiNoteTracker* MidiNoteManager::getTracker(uint8_t voiceId) {
    return (voiceId == 0) ? &voice1Tracker : &voice2Tracker;
}

const MidiNoteTracker* MidiNoteManager::getTracker(uint8_t voiceId) const {
    return (voiceId == 0) ? &voice1Tracker : &voice2Tracker;
}

void MidiNoteManager::sendMidiNoteOn(int8_t midiNote, uint8_t velocity, uint8_t channel) {
    if (midiNote >= 0 && midiNote <= 127) {
        usb_midi.sendNoteOn(midiNote, velocity, channel);
    }
}

void MidiNoteManager::sendMidiNoteOff(int8_t midiNote, uint8_t channel) {
    if (midiNote >= 0 && midiNote <= 127) {
        usb_midi.sendNoteOff(midiNote, 0, channel);
    }
}

void MidiNoteManager::noteOn(uint8_t voiceId, int8_t midiNote, uint8_t velocity, uint8_t channel, uint16_t gateDuration) {
    MidiNoteTracker* tracker = getTracker(voiceId);
    if (!tracker) return;

    beginAtomicUpdate(voiceId);

    // Turn off any currently playing note first (monophonic behavior)
    if (tracker->isNoteActive()) {
        sendMidiNoteOff(tracker->activeMidiNote, tracker->activeChannel);
    }

    // Set up new note
    tracker->activeMidiNote = midiNote;
    tracker->activeVelocity = velocity;
    tracker->activeChannel = channel;
    tracker->state = MidiNoteState::ACTIVE;
    tracker->gateActive = true;
    tracker->gateStartTick = tracker->currentTick;
    tracker->gateDurationTicks = gateDuration;
    tracker->pendingNoteChange = false;

    // Send MIDI note-on
    sendMidiNoteOn(midiNote, velocity, channel);

    endAtomicUpdate(voiceId);
}

void MidiNoteManager::noteOff(uint8_t voiceId) {
    MidiNoteTracker* tracker = getTracker(voiceId);
    if (!tracker) return;

    beginAtomicUpdate(voiceId);
    processNoteOff(tracker);
    endAtomicUpdate(voiceId);
}

void MidiNoteManager::processNoteOff(MidiNoteTracker* tracker) {
    if (tracker->isNoteActive()) {
        // Send MIDI note-off
        sendMidiNoteOff(tracker->activeMidiNote, tracker->activeChannel);

        // Reset tracker state
        tracker->state = MidiNoteState::INACTIVE;
        tracker->gateActive = false;
        tracker->activeMidiNote = -1;
        tracker->pendingNoteChange = false;
    }
}

void MidiNoteManager::updateTiming(uint16_t currentTick) {
    // Update timing for both voices
    voice1Tracker.currentTick = currentTick;
    voice2Tracker.currentTick = currentTick;

    // Check for expired gates and turn off notes
    if (voice1Tracker.isGateExpired() && voice1Tracker.isNoteActive()) {
        beginAtomicUpdate(0);
        processNoteOff(&voice1Tracker);
        endAtomicUpdate(0);
    }

    if (voice2Tracker.isGateExpired() && voice2Tracker.isNoteActive()) {
        beginAtomicUpdate(1);
        processNoteOff(&voice2Tracker);
        endAtomicUpdate(1);
    }
}

void MidiNoteManager::setGateState(uint8_t voiceId, bool gateActive, uint16_t gateDuration) {
    MidiNoteTracker* tracker = getTracker(voiceId);
    if (!tracker) return;

    beginAtomicUpdate(voiceId);

    if (gateActive) {
        tracker->gateActive = true;
        tracker->gateStartTick = tracker->currentTick;
        if (gateDuration > 0) {
            tracker->gateDurationTicks = gateDuration;
        }
    } else {
        // Gate turned off - turn off note immediately
        if (tracker->isNoteActive()) {
            processNoteOff(tracker);
        }
        tracker->gateActive = false;
    }

    endAtomicUpdate(voiceId);
}

bool MidiNoteManager::isGateActive(uint8_t voiceId) const {
    const MidiNoteTracker* tracker = getTracker(voiceId);
    return tracker ? tracker->gateActive : false;
}

bool MidiNoteManager::isNoteActive(uint8_t voiceId) const {
    const MidiNoteTracker* tracker = getTracker(voiceId);
    return tracker ? tracker->isNoteActive() : false;
}

int8_t MidiNoteManager::getActiveNote(uint8_t voiceId) const {
    const MidiNoteTracker* tracker = getTracker(voiceId);
    return tracker ? tracker->activeMidiNote : -1;
}

void MidiNoteManager::allNotesOff() {
    // Turn off all active notes
    beginAtomicUpdate(0);
    processNoteOff(&voice1Tracker);
    endAtomicUpdate(0);

    beginAtomicUpdate(1);
    processNoteOff(&voice2Tracker);
    endAtomicUpdate(1);

    // Send MIDI All Notes Off message for safety
    usb_midi.sendControlChange(123, 0, 1); // All Notes Off on channel 1
}

void MidiNoteManager::voiceReset(uint8_t voiceId) {
    MidiNoteTracker* tracker = getTracker(voiceId);
    if (!tracker) return;

    beginAtomicUpdate(voiceId);

    // Turn off any active note
    if (tracker->isNoteActive()) {
        sendMidiNoteOff(tracker->activeMidiNote, tracker->activeChannel);
    }

    // Reset tracker
    tracker->reset();

    endAtomicUpdate(voiceId);
}

void MidiNoteManager::emergencyStop() {
    // Immediate stop without atomic updates for emergency situations
    if (voice1Tracker.isNoteActive()) {
        sendMidiNoteOff(voice1Tracker.activeMidiNote, voice1Tracker.activeChannel);
    }
    if (voice2Tracker.isNoteActive()) {
        sendMidiNoteOff(voice2Tracker.activeMidiNote, voice2Tracker.activeChannel);
    }

    voice1Tracker.reset();
    voice2Tracker.reset();

    // Send MIDI panic messages
    usb_midi.sendControlChange(120, 0, 1); // All Sound Off
    usb_midi.sendControlChange(123, 0, 1); // All Notes Off
}

void MidiNoteManager::beginAtomicUpdate(uint8_t voiceId) {
    MidiNoteTracker* tracker = getTracker(voiceId);
    if (tracker) {
        tracker->updateInProgress = true;
    }
}

void MidiNoteManager::endAtomicUpdate(uint8_t voiceId) {
    MidiNoteTracker* tracker = getTracker(voiceId);
    if (tracker) {
        tracker->updateInProgress = false;
    }
}

void MidiNoteManager::onSequencerStop() {
    // Comprehensive cleanup when sequencer stops
    allNotesOff();

    // Reset timing counters
    voice1Tracker.currentTick = 0;
    voice2Tracker.currentTick = 0;
    voice1Tracker.gateStartTick = 0;
    voice2Tracker.gateStartTick = 0;
}

void MidiNoteManager::onModeSwitch() {
    // Clean up when switching between voice modes
    allNotesOff();

    // Brief delay to ensure MIDI messages are sent
    // Note: In embedded systems, consider using a non-blocking approach
}

void MidiNoteManager::onParameterChange(uint8_t voiceId) {
    // Handle parameter changes during playback
    // For now, we don't need to turn off notes for parameter changes
    // unless it's a note or octave change, which is handled in updateVoiceParameters
}

void MidiNoteManager::onTempoChange() {
    // Handle tempo changes - may need to adjust gate timing
    // For now, let existing notes complete naturally
    // Future enhancement: could recalculate gate durations based on new tempo
}

// =======================
//   LEGACY COMPATIBILITY FUNCTIONS
// =======================

// Helper to convert a sequencer's final note value (scale index + octave) to a MIDI note
int getMidiNote(uint8_t finalNoteValue)
{
    uint8_t originalValue = finalNoteValue;
    // The scale array has 48 entries (0-47). Clamp the input to prevent out-of-bounds access.
    if (finalNoteValue >= 48)
    {
        finalNoteValue = 47;
    }

  

    // Base MIDI note C2 (36)
    return scale[currentScale][finalNoteValue] + 36;
}


// Enhanced allNotesOff function using MidiNoteManager
void allNotesOff()
{
    // Use MidiNoteManager for comprehensive note cleanup
    midiNoteManager.allNotesOff();

    // Update sequencer states
    if (seq1.isNotePlaying())
    {
        seq1.handleNoteOff(&voiceState1);
    }
    if (seq2.isNotePlaying())
    {
        seq2.handleNoteOff(&voiceState2);
    }

    // Stop gate timers and reset gate states
    gateTimer1.stop();
    gateTimer2.stop();
    GATE1 = false;
    GATE2 = false;
}

// =======================
//   MIDI CC IMPLEMENTATION
// =======================

void MidiNoteManager::updateParameterCC(uint8_t voiceId, ParamId paramId, float value) {
    // Validate inputs
    if (voiceId > 1) return; // Only support Voice 1 (0) and Voice 2 (1)

    // Only send CC for supported parameters (Filter, Attack, Decay, Octave)
    if (paramId != ParamId::Filter && paramId != ParamId::Attack &&
        paramId != ParamId::Decay && paramId != ParamId::Octave) {
        return;
    }

    // Send CC if value has changed
    sendCCIfChanged(voiceId, paramId, value);
}

void MidiNoteManager::sendCCIfChanged(uint8_t voiceId, ParamId paramId, float value) {
    // Validate inputs
    if (voiceId > 1) return;

    // Map ParamId to array index for CC state tracking
    int paramIndex = -1;
    switch (paramId) {
        case ParamId::Filter: paramIndex = 0; break;
        case ParamId::Attack: paramIndex = 1; break;
        case ParamId::Decay:  paramIndex = 2; break;
        case ParamId::Octave: paramIndex = 3; break;
        default: return; // Unsupported parameter
    }

    // Clamp value to valid range
    float clampedValue = clampParameterValue(value);

    // Check if transmission should occur
    if (!shouldTransmitCC(voiceId, paramId, clampedValue)) {
        return;
    }

    // Get CC number for this voice/parameter combination
    uint8_t ccNumber = getParameterCCNumber(voiceId, paramId);
    if (ccNumber == 0) return; // Invalid CC number

    // Scale to MIDI range (0-127)
    uint8_t midiValue = scaleParameterToMidi(paramId, clampedValue);

    // Send the CC message
    sendCC(ccNumber, midiValue);

    // Update state tracking
    CCParameterState& state = ccStates[voiceId][paramIndex];
    state.lastValue = clampedValue;
    state.lastTransmissionTime = millis();
}

void MidiNoteManager::sendCC(uint8_t ccNumber, uint8_t value, uint8_t channel) {
    // Validate parameters
    if (ccNumber > 127 || value > 127 || channel < 1 || channel > 16) {
        return;
    }

    // Send MIDI CC message via USB
    usb_midi.sendControlChange(ccNumber, value, channel);
}

uint8_t MidiNoteManager::getParameterCCNumber(uint8_t voiceId, ParamId paramId) {
    // CC mapping based on voice and parameter
    if (voiceId == 0) { // Voice 1
        switch (paramId) {
            case ParamId::Filter: return 74;
            case ParamId::Attack: return 73;
            case ParamId::Decay:  return 72;
            case ParamId::Octave: return 71;
            default: return 0;
        }
    } else { // Voice 2
        switch (paramId) {
            case ParamId::Filter: return 78;
            case ParamId::Attack: return 77;
            case ParamId::Decay:  return 76;
            case ParamId::Octave: return 75;
            default: return 0;
        }
    }
}

uint8_t MidiNoteManager::scaleParameterToMidi(ParamId paramId, float value) {
    // Scale normalized value (0.0-1.0) to MIDI range (0-127)
    float scaledValue = value * 127.0f;
    return static_cast<uint8_t>(std::max(0.0f, std::min(scaledValue, 127.0f)));
}

bool MidiNoteManager::shouldTransmitCC(uint8_t voiceId, ParamId paramId, float value) {
    // Map ParamId to array index
    int paramIndex = -1;
    switch (paramId) {
        case ParamId::Filter: paramIndex = 0; break;
        case ParamId::Attack: paramIndex = 1; break;
        case ParamId::Decay:  paramIndex = 2; break;
        case ParamId::Octave: paramIndex = 3; break;
        default: return false;
    }

    CCParameterState& state = ccStates[voiceId][paramIndex];

    // Check if enough time has passed since last transmission
    unsigned long currentTime = millis();
    if (currentTime - state.lastTransmissionTime < 10) { // 10ms minimum interval
        return false;
    }

    // Check if value has changed significantly
    float valueDiff = std::abs(value - state.lastValue);
    if (valueDiff < 0.01f) { // 1% threshold
        return false;
    }

    return true;
}

float MidiNoteManager::clampParameterValue(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}
