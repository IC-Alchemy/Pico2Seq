#ifndef SEQUENCER_H
#define SEQUENCER_H
#include "SequencerDefs.h"
#include "ParameterManager.h"

/**
 * @brief Simple envelope controller for ADSR triggering
 *
 * Manages envelope trigger/release states for note events.
 * Uses stack allocation for embedded performance.
 */
class EnvelopeController
{
public:
    void trigger()
    {
        triggered = true;
        released = false;
    }
    void release()
    {
        triggered = false;
        released = true;
    }
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
class NoteDurationTracker
{
public:
    void start(uint16_t duration)
    {
        counter = duration;
        active = true;
    }
    void tick()
    {
        if (active && counter > 0)
        {
            --counter;
            if (counter == 0)
                active = false;
        }
    }
    bool isActive() const { return active && counter > 0; }
    void reset()
    {
        counter = 0;
        active = false;
    }

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
class Sequencer
{
public:
    Sequencer();
    Sequencer(uint8_t channel);
    ~Sequencer() = default;

    // Parameter management
    void initializeParameters();
    void resetAllSteps();

    /**
     * @brief Play a specific step immediately (preview mode)
     * @param stepIdx Step index to play (0-63)
     * @param voiceState Output voice state structure
     */
    void playStepNow(uint8_t stepIdx, VoiceState *voiceState);

    /**
     * @brief Preview active step using current independent polymetric parameter cursors
     * @param voiceState Output voice state structure
     */
    void previewActiveStep(VoiceState *voiceState);

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
    void handleNoteOff(VoiceState *voiceState);
    // Ticks the note-duration countdown; returns true on the tick where the
    // gate length expired (handleNoteOff already ran), so callers can push
    // the mid-step note-off to the audio voice.
    bool tickNoteDuration(VoiceState *voiceState);
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
     * 5. Apply magnetic encoder modifications (done in main loop)
     *
     * @param current_uclock_step Global step counter from UClock (full 32-bit range;
     *                            truncated to uint8_t only after per-track modulo)
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
    void advanceStep(uint32_t current_uclock_step, int mm_distance,
                     bool is_note_button_held, bool is_velocity_button_held,
                     bool is_filter_button_held, bool is_attack_button_held,
                     bool is_decay_button_held, bool is_octave_button_held,
                     int current_selected_step_for_edit,
                     VoiceState *voiceState);

    uint8_t getCurrentStep() const { return currentStep; }
    int8_t getCurrentNote() const { return currentNote; }

    /**
     * @brief Get current step position for a specific parameter
     * @param paramId Parameter to query
     * @return Current step index for the parameter (0 to stepCount-1)
     */
    uint8_t getCurrentStepForParameter(ParamId paramId) const;

    // Get step data
    Step getStep(uint8_t stepIdx) const;
    // Same composed values used by playback; UINT8_MAX follows independent tracks.
    // Read-only: does not trigger gates, advance transport or alter note tails.
    Step getPlaybackStep(uint8_t stepIdx = UINT8_MAX) const;

    // State
    bool isRunning() const { return running; }

    // Optional control-thread playback mapping. Stored step data is never
    // changed. The context must outlive this sequencer. No synth/UI dependency.
    using PlaybackTransform = float (*)(ParamId, float, const void *);
    using OctaveMapper = int8_t (*)(float);
    void setPlaybackTransform(PlaybackTransform transform, const void *context,
                              OctaveMapper octaveMapper = nullptr) noexcept
    { playbackTransform_ = transform; playbackContext_ = context; octaveMapper_ = octaveMapper; }
    // Calibrated normalized recording input, consumed by the next advance.
    void setRecordingInput(float normalized) noexcept { recordingInput_ = normalized; }
    // Initialization/reset only: unlike note recording this also initializes
    // silent steps, so enabling a gate later reveals a neutral modifier.
    void fillModulationTrack(ParamId id, float value) {
        for(uint8_t step=0;step<SequencerConstants::MAX_STEPS_COUNT;++step)
            parameterManager.setValue(id,step,value);
    }
    bool usesPlaybackTransform() const noexcept { return playbackTransform_ != nullptr; }
    void resetModifierStep(uint8_t step) {
        if(step>=SequencerConstants::MAX_STEPS_COUNT) return;
        for(uint8_t i=0;i<PARAM_ID_COUNT;++i) {
            const auto id=static_cast<ParamId>(i);
            parameterManager.setValue(id,step,(id==ParamId::Gate || id==ParamId::Slide || id==ParamId::Note)?0.0f:
                mapNormalizedValueToParamRange(id,0.5f));
        }
    }

private:
    PlaybackTransform playbackTransform_ = nullptr;
    const void *playbackContext_ = nullptr;
    OctaveMapper octaveMapper_ = nullptr;
    float recordingInput_ = -1.0f;
    float playbackValue(ParamId id, uint8_t step) const
    {
        const float stored = getStepParameterValue(id, step);
        return playbackTransform_ ? playbackTransform_(id, stored, playbackContext_) : stored;
    }
    void (*midiNoteOffCallback)(uint8_t note, uint8_t channel) = nullptr;

    // Envelope methods
    void triggerEnvelope();
    void releaseEnvelope();

private:
    ParameterManager parameterManager;
    EnvelopeController envelope;
    bool running;
    uint8_t currentStep;                                              // Global step counter (used for Gate parameter timing)
    uint8_t currentStepPerParam[static_cast<size_t>(ParamId::Count)]; // Independent step counters for each parameter
    int8_t lastNote;
    int8_t currentNote;
    // currentNote's range includes negatives (note 0 with the -12 octave
    // offset is -12), so a sign check cannot mean "no note"; this flag does.
    bool noteActive;
    uint16_t noteDurationCounter;
    uint8_t channel;
    NoteDurationTracker noteDuration;
    bool previousStepHadSlide; // Track if previous step had slide enabled

    // Internal methods
    void processStep(uint8_t stepIdx, VoiceState *voiceState);
};

#endif // SEQUENCER_H
