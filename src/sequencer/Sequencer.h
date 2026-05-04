#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "ParameterManager.h"
#include "SequencerDefs.h"

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

class NoteDurationTracker {
public:
    void start(uint16_t duration) { counter = duration; active = true; }
    void tick()
    {
        if (active && counter > 0) {
            --counter;
            if (counter == 0) {
                active = false;
            }
        }
    }
    bool isActive() const { return active && counter > 0; }
    void reset() { counter = 0; active = false; }

private:
    uint16_t counter = 0;
    bool active = false;
};

class Sequencer {
public:
    Sequencer();
    explicit Sequencer(uint8_t channel);
    ~Sequencer() = default;

    void initializeParameters();
    void resetAllSteps();
    void resetSequencer() { reset(); }

    void playStepNow(uint8_t stepIdx, VoiceState *voiceState);
    void toggleStep(uint8_t stepIdx);

    float getStepParameterValue(ParamId id, uint8_t stepIdx) const;
    void setStepParameterValue(ParamId id, uint8_t stepIdx, float value);
    uint8_t getParameterStepCount(ParamId id) const;
    void setParameterStepCount(ParamId id, uint8_t steps);

    void start() { running = true; }
    void stop() { running = false; }
    void reset();
    void randomizeParameters();

    void startNote(uint8_t note, uint8_t velocity, uint16_t duration);
    void handleNoteOff(VoiceState *voiceState);
    void tickNoteDuration(VoiceState *voiceState);
    bool isNotePlaying() const;

    void setMidiNoteOffCallback(void (*callback)(uint8_t note, uint8_t channel));
    void advanceStep(uint8_t current_uclock_step, const ParameterInputState &input, VoiceState *voiceState);

    uint8_t getCurrentStep() const { return currentStep; }
    uint8_t getCurrentStepForParameter(ParamId paramId) const;
    Step getStep(uint8_t stepIdx) const;
    bool isRunning() const { return running; }

private:
    void commonInit();
    void triggerEnvelope();
    void releaseEnvelope();
    void processStep(uint8_t stepIdx, VoiceState *voiceState);

    void (*midiNoteOffCallback)(uint8_t note, uint8_t channel) = nullptr;
    ParameterManager parameterManager;
    EnvelopeController envelope;
    bool running = false;
    uint8_t currentStep = 0;
    uint8_t currentStepPerParam[static_cast<size_t>(ParamId::Count)] = {};
    int8_t lastNote = -1;
    int8_t currentNote = -1;
    uint16_t noteDurationCounter = 0;
    uint8_t channel = 0;
    NoteDurationTracker noteDuration;
    bool previousStepHadSlide = false;
};

#endif // SEQUENCER_H
