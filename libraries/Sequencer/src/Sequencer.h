#ifndef SEQUENCER_H
#define SEQUENCER_H
#include "SequencerDefs.h"
#include "ParameterManager.h"
#include "../ui/UIState.h"

/**
 * @brief Simple envelope controller for ADSR triggering
 *
 * Manages envelope trigger/release states for note events.
 * Uses stack allocation for embedded performance.
 */
class EnvelopeController {
public:
    void trigger() { triggered = true; released = false; }
    void release() { triggered = false; released = true; }
    bool isTriggered() const { return triggered; }
    bool isReleased() const { return released; }
private:
    bool triggered = false;
    bool released = true;
};

/**
 * @brief Note duration tracker for gate timing
 *
 * Tracks note duration in sequencer pulses for precise gate timing.
 * Automatically deactivates when duration expires.
 */
class NoteDurationTracker {
public:
    void start(uint16_t duration) { counter = duration; active = true; }
    void tick() { if (active && counter > 0) { --counter; if (counter == 0) active = false; } }
    bool isActive() const { return active && counter > 0; }
    void reset() { counter = 0; active = false; }
private:
    uint16_t counter = 0;
    bool active = false;
};

/**
 * @brief Polyrhythmic step sequencer with independent parameter tracks
 *
 * The Sequencer class implements the core sequencing logic for the PicoMudrasSequencer.
 * Each parameter (Note, Velocity, Filter, Attack, Decay, Octave, GateLength, Gate, Slide)
 * operates as an independent track with configurable step counts, enabling complex
 * polyrhythmic patterns that evolve over hundreds of steps.
 *
 * Key Features:
 * - Independent parameter track advancement (polyrhythmic sequencing)
 * - Real-time parameter recording via sensor input
 * - Thread-safe operation for dual-core architecture
 * - Dual voice support for layered compositions
 */
class Sequencer {
public:
    Sequencer();
    Sequencer(uint8_t channel);
    ~Sequencer() = default;
    void resetSequencer();

    // Parameter management
    void initializeParameters();
    void resetAllSteps();

    /**
     * @brief Play a specific step immediately (preview mode)
     * @param stepIdx Step index to play (0-63)
     * @param voiceState Output voice state structure
     */
    void playStepNow(uint8_t stepIdx,  VoiceState* voiceState);

    /**
     * @brief Toggle gate parameter for a specific step
     * @param stepIdx Step index to toggle (0-63)
     */
    void toggleStep(uint8_t stepIdx);

    // Parameter access methods
    float getStepParameterValue(ParamId id, uint8_t stepIdx) const;
    void setStepParameterValue(ParamId id, uint8_t stepIdx, float value);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);

    // Sequencer control
    void start() { running = true; }
    void stop() { running = false; }
    void reset();
    void randomizeParameters();
    // Note/Envelope handling
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff( VoiceState* voiceState);
    void tickNoteDuration( VoiceState* voiceState);
    bool isNotePlaying() const;

    // MIDI callback function pointers for note-off events
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    /**
     * @brief Advance sequencer by one step with polyrhythmic parameter tracking
     *
     * This is the core sequencing method that implements independent parameter
     * track advancement. Each parameter advances at its own configured step count,
     * enabling complex polyrhythmic patterns.
     *
     * Data Flow:
     * 1. Calculate currentStepPerParam[i] = uclock_step % paramStepCount[i] for each parameter
     * 2. Handle real-time parameter recording if buttons are held
     * 3. Process step using independent parameter positions
     * 4. Update VoiceState with current parameter values
     * 5. Apply AS5600 encoder modifications (done in main loop)
     *
     * @param current_uclock_step Global step counter from UClock
     * @param mm_distance Distance sensor reading (0-400mm range)
     * @param is_note_button_held Button 16 state for Note parameter recording
     * @param is_velocity_button_held Button 17 state for Velocity parameter recording
     * @param is_filter_button_held Button 18 state for Filter parameter recording
     * @param is_attack_button_held Button 19 state for Attack parameter recording
     * @param is_decay_button_held Button 20 state for Decay parameter recording
     * @param is_octave_button_held Button 21 state for Octave parameter recording
     * @param current_selected_step_for_edit Selected step for editing (-1 for real-time mode)
     * @param voiceState Output voice state structure for audio synthesis
     */
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                     bool is_note_button_held, bool is_velocity_button_held,
                     bool is_filter_button_held, bool is_attack_button_held,
                     bool is_decay_button_held, bool is_octave_button_held,
                     int current_selected_step_for_edit,
                      VoiceState *voiceState);

    /**
     * @brief Advance sequencer by one step using UIState for button states
     *
     * This overload extracts button states from UIState and calls the main advanceStep method.
     * Provides a cleaner interface when UIState is available.
     *
     * @param current_uclock_step Global step counter from UClock
     * @param mm_distance Distance sensor reading (0-400mm range)
     * @param uiState UI state containing button states and selected step
     * @param voiceState Output voice state structure for audio synthesis
  
     */
    void advanceStep(uint8_t current_uclock_step, int mm_distance,
                     const UIState& uiState, VoiceState *voiceState);

    uint8_t getCurrentStep() const { return currentStep; }

    /**
     * @brief Get current step position for a specific parameter
     * @param paramId Parameter to query
     * @return Current step index for the parameter (0 to stepCount-1)
     */
    uint8_t getCurrentStepForParameter(ParamId paramId) const;


    // Get step data
    Step getStep(uint8_t stepIdx) const;

    // State
    bool isRunning() const { return running; }

private:
    void (*midiNoteOffCallback)(uint8_t note, uint8_t channel) = nullptr;

    // Envelope methods
    void triggerEnvelope();
    void releaseEnvelope();

private:
    ParameterManager parameterManager;
    EnvelopeController envelope;
    bool running;
    uint8_t currentStep; // Global step counter (used for Gate parameter timing)
    uint8_t currentStepPerParam[static_cast<size_t>(ParamId::Count)]; // Independent step counters for each parameter
    int8_t lastNote;
    int8_t currentNote;
    uint16_t noteDurationCounter;
    uint8_t channel;
    NoteDurationTracker noteDuration;
    bool previousStepHadSlide; // Track if previous step had slide enabled


    // Internal methods
    void processStep(uint8_t stepIdx,  VoiceState* voiceState);
};

#endif // SEQUENCER_H
