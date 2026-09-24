#ifndef SEQUENCER_H
#define SEQUENCER_H
// Sequencer: one polymetric pattern (notes, tone, articulation per step).
// Four instances make the 4-voice song; Core 0 advances steps, Core 1 renders.
#include "SequencerDefs.h"
#include "ParameterManager.h"

/**
 * @brief Gate flip-flop: retrigger a step's envelope, then release its tail.
 * Stack-only; one per Sequencer, driven by gate on/off in processStep().
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
 * @brief Gate-length countdown in clock ticks: how long a step holds (legato)
 * vs. chokes (staccato). Expires to silent automatically at zero.
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
 * @brief One voice's polymetric pattern: every ParamId loops at its own length.
 *
 * Musically this is what lets Note run 16 steps while Filter loops 8 — the
 * phrase and the tone drift in and out of phase over many bars. Storage is
 * fixed-size (no heap in the step path); transport runs on Core 0 and hands a
 * VoiceState to the audio engine. One instance per voice (4 total).
 */
class Sequencer
{
public:
    Sequencer();
    Sequencer(uint8_t channel);
    ~Sequencer() = default;

    // Lane values and lengths
    void initializeParameters();
    void resetAllSteps();

    /**
     * @brief Restore fresh-boot pattern: defaults in all 64 slots, gates/slides
     * off, 16-step lengths. In patch mode lanes neutralize around the patch
     * base instead. Releases any sounding note; transport/voice config kept.
     */
    void clearPattern();

    /**
     * @brief Audition one stored step now (pads/preview key); plays it as if
     * the transport had landed on it.
     * @param stepIdx Step to hear (0-63)
     */
    void playStepNow(uint8_t stepIdx, VoiceState *voiceState);

    /**
     * @brief Audition the currently sounding combination of lane cursors
     * (each lane may sit on a different step under polymeter).
     */
    void previewActiveStep(VoiceState *voiceState);

    /**
     * @brief Apply live lane edits to a sounding voice without retriggering.
     * Pitch follows only while the gate is high, so tweaks never restart the
     * envelope mid-note.
     */
    void refreshVoiceParameters(VoiceState *voiceState) const;

    /**
     * @brief Flip a step between sounding and resting (gate on/off).
     * @param stepIdx Step to flip (0-63)
     */
    void toggleStep(uint8_t stepIdx);

    // Lane values (wrapping read) and lengths
    float getStepParameterValue(ParamId id, uint8_t stepIdx) const;
    void setStepParameterValue(ParamId id, uint8_t stepIdx, float value);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);
    // Save/load path: unwrapped read, unclamped write (values were normalized
    // at record time). Writes at/beyond the active length wrap — the codec
    // grows to MAX first so restore never wraps.
    float getRawStepValue(ParamId id, uint8_t stepIdx) const;
    void setRawStepValue(ParamId id, uint8_t stepIdx, float value);

    /**
     * @brief Overdub one lane at its currently sounding cursor.
     * Note lands only while the Gate lane's step sounds, so silent steps keep
     * their pitch. Returns true when the stored value actually changed.
     */
    bool recordLiveValue(ParamId id, float value);

    /**
     * @brief Edit one stored step; Note lands only on a sounding (gated) step.
     * Returns true when the stored value actually changed.
     */
    bool editStepValue(ParamId id, uint8_t stepIdx, float value);

    /**
     * @brief Return one patch-following lane (Velocity/Filter/ADSR) to the patch.
     * Returns true when the step held its own value before.
     */
    bool followPatch(ParamId id, uint8_t stepIdx);

    // What one step actually plays (through the patch transform when set).
    float getPlaybackValue(ParamId id, uint8_t stepIdx) const;
    // What a patch-following step plays: patch value, else the lane default.
    float patchValue(ParamId id) const;

    // Transport
    void start() { running = true; }
    void stop() { running = false; }
    void reset();
    void randomizeParameters(uint8_t depthPercent = ParameterManager::kDefaultRandomizeDepth,
                             uint64_t seed = 0);
    // Note/gate lifecycle
    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState *voiceState);
    // Mid-step gate-off: ticks the hold countdown; true on the expiring tick
    // (note already released) so the caller can push it to the audio voice.
    bool tickNoteDuration(VoiceState *voiceState);
    bool isNotePlaying() const;

    // Legacy hook for external note-off routing; audio path uses VoiceState.
    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));

    /**
     * @brief Step the transport once: advance every lane cursor, overdub any
     * held record buttons, then sound the resulting combination into voiceState.
     *
     * Each lane wraps on its own length (uclock_step % laneLength), so lanes of
     * different lengths phase against each other. UINT8_MAX inside processStep
     * means "use each lane's own cursor" rather than one shared step.
     *
     * @param current_uclock_step Global clock counter (full 32-bit; narrow only
     *                            after per-lane modulo to avoid 256-step aliasing)
     * @param mm_distance Hand distance in mm (0-400 musical zone; <0 disables)
     * @param is_note_button_held etc: which lanes the held buttons overdub
     * @param current_selected_step_for_edit Step-edit target, or -1 for live record
     * @param voiceState Played-step output for the audio engine
     */
    void advanceStep(uint32_t current_uclock_step, int mm_distance,
                     bool is_note_button_held, bool is_velocity_button_held,
                     bool is_filter_button_held, bool is_attack_button_held,
                     bool is_release_button_held, bool is_octave_button_held,
                     int current_selected_step_for_edit,
                     VoiceState *voiceState);

    uint8_t getCurrentStep() const { return currentStep; }
    int8_t getCurrentNote() const { return currentNote; }

    /**
     * @brief This lane's currently sounding step (0 to laneLength-1).
     */
    uint8_t getCurrentStepForParameter(ParamId paramId) const;

    // Stored step data
    Step getStep(uint8_t stepIdx) const;
    void setStep(uint8_t stepIdx, const Step &step);
    void copyStep(uint8_t srcStep, uint8_t dstStep);
    // Played values (through the patch transform); UINT8_MAX = lane cursors.
    // Read-only: never triggers gates, moves transport, or cuts note tails.
    Step getPlaybackStep(uint8_t stepIdx = UINT8_MAX) const;

    // State
    bool isRunning() const { return running; }

    // Patch transform for patch mode: maps stored steps to played values without
    // rewriting storage. Context must outlive this sequencer; no synth/UI types.
    using PlaybackTransform = float (*)(ParamId, float, const void *);
    using OctaveMapper = int8_t (*)(float);
    void setPlaybackTransform(PlaybackTransform transform, const void *context,
                              OctaveMapper octaveMapper = nullptr) noexcept
    { playbackTransform_ = transform; playbackContext_ = context; octaveMapper_ = octaveMapper; }
    // Hand-calibrated record input, consumed once by the next step.
    void setRecordingInput(float normalized) noexcept { recordingInput_ = normalized; }
    // Init/reset helper: also seeds resting steps so unmuting a gate later
    // reveals the neutral value; doubles as the fill for grown track tails.
    void fillModulationTrack(ParamId id, float value) { parameterManager.fillTrack(id, value); }
    bool usesPlaybackTransform() const noexcept { return playbackTransform_ != nullptr; }
    // Resting step in patch mode: patch lanes follow, offsets sit centered,
    // Gate/Slide rest off.
    void resetModifierStep(uint8_t step) {
        if(step>=SequencerConstants::MAX_STEPS_COUNT) return;
        for(uint8_t i=0;i<PARAM_ID_COUNT;++i) {
            const auto id=static_cast<ParamId>(i);
            parameterManager.setValue(id,step,
                isPatchDefaultLane(id) ? SequencerConstants::LANE_FOLLOWS_PATCH :
                (id==ParamId::Gate || id==ParamId::Slide || id==ParamId::Note) ? 0.0f :
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

    // Envelope trigger/release driven by gate on/off
    void triggerEnvelope();
    void releaseEnvelope();

private:
    ParameterManager parameterManager;
    EnvelopeController envelope;
    bool running;
    uint8_t currentStep; // Bar position from the Gate lane (UI/LED cursor)
    uint8_t currentStepPerParam[static_cast<size_t>(ParamId::Count)]; // Sounding step per lane
    int8_t lastNote;
    int8_t currentNote;
    // Separate flag (not a sentinel): transposed notes legitimately go negative.
    bool noteActive;
    uint16_t noteDurationCounter;
    uint8_t channel;
    NoteDurationTracker noteDuration;
    bool previousStepHadSlide; // Lets a slide ring through a following rest

    // Step sound engine (not transport): gate/slide/note lifecycle + VoiceState out
    void processStep(uint8_t stepIdx, VoiceState *voiceState);
    bool gateIsOn(uint8_t stepIdx) const;
    bool writeStepValue(ParamId id, uint8_t stepIdx, float value);
};

#endif // SEQUENCER_H
