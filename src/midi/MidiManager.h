#ifndef MIDI_MANAGER_H
#define MIDI_MANAGER_H

#include <cstdint>
#include <MIDI.h>
#include <Adafruit_TinyUSB.h>
#include "../sequencer/SequencerDefs.h" // For VoiceState and GateTimer definitions
#include "MidiCCConfig.h" // MIDI CC configuration constants

// =======================
//   MIDI CC CONFIGURATION
// =======================

/**
 * MIDI CC Configuration for PicoMudrasSequencer
 *
 * CC Number Mapping Strategy:
 * - Voice 1: CC71-74 (Filter=74, Attack=73, Decay=72, Octave=71)
 * - Voice 2: CC75-78 (Filter=78, Attack=77, Decay=76, Octave=75)
 *
 * All CC messages are sent on MIDI Channel 1 for simplicity.
 * External DAWs/hardware can differentiate voices by CC number ranges.
 *
 * All configuration constants are defined in MidiCCConfig.h
 */

// CC parameter index mapping for array access
enum class CCParameterIndex : uint8_t {
    FILTER = 0,  // Index 0 in ccStates array
    ATTACK = 1,  // Index 1 in ccStates array
    DECAY = 2,   // Index 2 in ccStates array
    OCTAVE = 3,  // Index 3 in ccStates array
    COUNT = 4    // Total number of CC parameters
};

/**
 * @brief State tracking for MIDI CC parameter transmission
 *
 * This structure maintains the state needed for intelligent CC transmission,
 * including change detection and rate limiting. One instance exists for each
 * parameter of each voice (2 voices × 4 parameters = 8 total instances).
 */
struct CCParameterState {
    float lastValue = 0.0f;                    ///< Last raw parameter value (0.0f - 1.0f)
    uint8_t lastMidiValue = 0;                 ///< Last transmitted MIDI value (0-127)
    bool hasChanged = false;                   ///< Flag indicating value has changed since last check
    unsigned long lastTransmissionTime = 0;   ///< Timestamp of last CC transmission (milliseconds)
    uint32_t changeCount = 0;                  ///< Total number of changes detected (for debugging)
    bool isInitialized = false;                ///< Flag to track first value assignment

    /**
     * @brief Reset state to initial values
     * Called during initialization or when resetting CC state.
     */
    void reset() {
        lastValue = 0.0f;
        lastMidiValue = 0;
        hasChanged = false;
        lastTransmissionTime = 0;
        changeCount = 0;
        isInitialized = false;
    }

    /**
     * @brief Check if this is the first time setting a value
     * @return true if no value has been set yet
     */
    bool isFirstValue() const {
        return !isInitialized;
    }

    /**
     * @brief Mark parameter as initialized with first value
     * Called after the first parameter value is processed.
     */
    void markInitialized() {
        isInitialized = true;
    }
};

// Forward declarations
class Sequencer;

// =======================
//   ENHANCED MIDI NOTE TRACKING STRUCTURES
// =======================

/**
 * @brief State of an active MIDI note
 */
enum class MidiNoteState : uint8_t {
    INACTIVE = 0,    // No note playing
    ACTIVE = 1,      // Note is currently playing
    PENDING_OFF = 2  // Note should be turned off on next update
};

/**
 * @brief Comprehensive MIDI note tracking for a single voice
 *
 * This structure maintains complete state information for MIDI note lifecycle
 * management, ensuring proper note-on/off pairing and gate timing synchronization.
 */
struct MidiNoteTracker {
    // Note identification
    volatile int8_t activeMidiNote = -1;        // Currently playing MIDI note (-1 = none)
    volatile uint8_t activeVelocity = 0;        // Velocity of active note
    volatile uint8_t activeChannel = 1;         // MIDI channel for this voice

    // State management
    volatile MidiNoteState state = MidiNoteState::INACTIVE;
    volatile bool gateActive = false;           // Current gate state
    volatile bool pendingNoteChange = false;    // Flag for note change during gate

    // Timing synchronization
    volatile uint16_t gateStartTick = 0;        // Tick when gate opened
    volatile uint16_t gateDurationTicks = 0;    // Expected gate duration
    volatile uint16_t currentTick = 0;          // Current timing tick

    // Thread safety
    volatile bool updateInProgress = false;     // Atomic update flag

    /**
     * @brief Check if a note is currently active
     */
    bool isNoteActive() const volatile {
        return state == MidiNoteState::ACTIVE && activeMidiNote >= 0;
    }

    /**
     * @brief Check if gate timing has expired
     */
    bool isGateExpired() const volatile {
        return gateActive && (currentTick >= (gateStartTick + gateDurationTicks));
    }

    /**
     * @brief Reset tracker to inactive state
     */
    void reset() volatile {
        activeMidiNote = -1;
        activeVelocity = 0;
        state = MidiNoteState::INACTIVE;
        gateActive = false;
        pendingNoteChange = false;
        updateInProgress = false;
    }
};

/**
 * @brief Centralized MIDI note management system
 *
 * Handles all MIDI note lifecycle events with proper synchronization
 * between gate timing and note-on/off events.
 */
class MidiNoteManager {
public:
    MidiNoteManager();

    // Core note management
    void noteOn(uint8_t voiceId, int8_t midiNote, uint8_t velocity, uint8_t channel, uint16_t gateDuration);
    void noteOff(uint8_t voiceId);
    void updateTiming(uint16_t currentTick);

    // Gate synchronization
    void setGateState(uint8_t voiceId, bool gateActive, uint16_t gateDuration = 0);
    bool isGateActive(uint8_t voiceId) const;

    // State queries
    bool isNoteActive(uint8_t voiceId) const;
    int8_t getActiveNote(uint8_t voiceId) const;

    // Cleanup and safety
    void allNotesOff();
    void voiceReset(uint8_t voiceId);
    void emergencyStop();

    // Additional cleanup methods for specific scenarios
    void onSequencerStop();           // Called when sequencer stops
    void onModeSwitch();              // Called when switching between voice modes
    void onParameterChange(uint8_t voiceId); // Called when parameters change during playback
    void onTempoChange();             // Called when tempo changes significantly

    // Thread safety
    void beginAtomicUpdate(uint8_t voiceId);
    void endAtomicUpdate(uint8_t voiceId);

    // =======================
    //   MIDI CC FUNCTIONALITY
    // =======================

    /**
     * @brief Main entry point for MIDI CC parameter updates
     * @param voiceId Voice identifier (0 = Voice 1, 1 = Voice 2)
     * @param paramId Parameter type (Filter, Attack, Decay, Octave)
     * @param value Normalized parameter value (0.0f - 1.0f)
     *
     * This method handles the complete CC transmission pipeline:
     * - Validates parameter type and voice ID
     * - Checks for value changes to prevent MIDI spam
     * - Scales parameter value to MIDI range (0-127)
     * - Transmits CC message if conditions are met
     */
    void updateParameterCC(uint8_t voiceId, ParamId paramId, float value);

    /**
     * @brief Conditionally send CC message if parameter value has changed
     * @param voiceId Voice identifier (0 = Voice 1, 1 = Voice 2)
     * @param paramId Parameter type to transmit
     * @param value Current parameter value (normalized 0.0f - 1.0f)
     *
     * Implements change detection and rate limiting to ensure efficient
     * MIDI transmission without overwhelming the USB MIDI buffer.
     */
    void sendCCIfChanged(uint8_t voiceId, ParamId paramId, float value);

    /**
     * @brief Low-level CC message transmission
     * @param ccNumber MIDI CC number (0-127)
     * @param value MIDI CC value (0-127)
     * @param channel MIDI channel (1-16, defaults to 1)
     *
     * Validates parameters and sends raw MIDI CC message via USB.
     */
    void sendCC(uint8_t ccNumber, uint8_t value, uint8_t channel = 1);

    /**
     * @brief Get MIDI CC number for a specific voice and parameter
     * @param voiceId Voice identifier (0 = Voice 1, 1 = Voice 2)
     * @param paramId Parameter type
     * @return MIDI CC number (71-78 range)
     *
     * Maps voice/parameter combinations to specific CC numbers:
     * Voice 1: CC71-74, Voice 2: CC75-78
     */
    uint8_t getParameterCCNumber(uint8_t voiceId, ParamId paramId);

    /**
     * @brief Convert normalized parameter value to MIDI CC range
     * @param paramId Parameter type (for future parameter-specific scaling)
     * @param value Normalized parameter value (0.0f - 1.0f)
     * @return MIDI CC value (0-127)
     *
     * All parameters use linear scaling since they're stored as normalized values.
     * Future versions may implement parameter-specific scaling curves.
     */
    uint8_t scaleParameterToMidi(ParamId paramId, float value);

    /**
     * @brief Determine if CC transmission should occur
     * @param voiceId Voice identifier
     * @param paramId Parameter type
     * @param value Current parameter value
     * @return true if CC should be transmitted, false otherwise
     *
     * Implements change detection and rate limiting:
     * - Only transmit if MIDI value actually changed
     * - Respect minimum transmission interval (10ms)
     * - Prevent MIDI spam during rapid parameter changes
     */
    bool shouldTransmitCC(uint8_t voiceId, ParamId paramId, float value);

    /**
     * @brief Clamp parameter value to valid normalized range
     * @param value Input parameter value
     * @return Clamped value (0.0f - 1.0f)
     */
    float clampParameterValue(float value);

    /**
     * @brief Check if parameter type supports CC transmission
     * @param paramId Parameter type to check
     * @return true if parameter supports CC output
     */
    bool isValidParameterForCC(ParamId paramId);

    /**
     * @brief Get human-readable parameter name for debugging
     * @param paramId Parameter type
     * @return Parameter name string
     */
    const char* getParameterName(ParamId paramId);

    /**
     * @brief Reset all CC state tracking to initial values
     * Called during initialization and when resetting the sequencer.
     */
    void resetCCStates();

    /**
     * @brief Output debug information for CC transmission
     * @param voice Voice number (0-1)
     * @param ccNumber MIDI CC number that was transmitted
     * @param value MIDI CC value that was transmitted
     *
     * Controlled by CC_DEBUG_ENABLED configuration flag.
     */
    void debugCCTransmission(uint8_t voice, uint8_t ccNumber, uint8_t value);

private:
    // MIDI note tracking for dual voice management
    MidiNoteTracker voice1Tracker;  ///< Note state tracking for Voice 1
    MidiNoteTracker voice2Tracker;  ///< Note state tracking for Voice 2

    /**
     * @brief CC parameter state tracking array
     *
     * Organization: ccStates[voiceId][parameterIndex]
     * - voiceId: 0 = Voice 1, 1 = Voice 2
     * - parameterIndex: 0 = Filter, 1 = Attack, 2 = Decay, 3 = Octave
     *
     * This 2D array maintains change detection and rate limiting state
     * for all CC-enabled parameters across both voices.
     */
    CCParameterState ccStates[2][4];

    // Internal helpers
    MidiNoteTracker* getTracker(uint8_t voiceId);
    const MidiNoteTracker* getTracker(uint8_t voiceId) const;
    void sendMidiNoteOn(int8_t midiNote, uint8_t velocity, uint8_t channel);
    void sendMidiNoteOff(int8_t midiNote, uint8_t channel);
    void processNoteOff(MidiNoteTracker* tracker);
};

// =======================
//   EXTERN DECLARATIONS
// =======================

extern uint8_t currentScale;

// Enhanced MIDI note management
extern MidiNoteManager midiNoteManager;

// USB MIDI interface
extern midi::MidiInterface<midi::SerialMIDI<Adafruit_USBD_MIDI>> usb_midi;

// Sequencer objects
extern Sequencer seq1;
extern Sequencer seq2;

// VoiceState objects for inter-core communication
extern  VoiceState voiceState1;
extern  VoiceState voiceState2;

// Gate control variables
extern  GateTimer gateTimer1;
extern  GateTimer gateTimer2;
extern volatile bool GATE1;
extern volatile bool GATE2;

// =======================
//   FUNCTION DECLARATIONS
// =======================

// Legacy compatibility functions (will be refactored to use MidiNoteManager)
int getMidiNote(uint8_t finalNoteValue);

#endif // MIDI_MANAGER_H
