#pragma once
// Desktop replacement for the uClock Arduino library (midilab/uClock 2.2.1).
//
// Same API surface the firmware uses. The implementation lives in
// desktop/native/src/HostClock.cpp: a faithful port of the library's
// internal-clock timing core (handleTimerInt + processShuffle) driven by
// the audio callback's sample counter instead of a hardware timer.

#include <stdint.h>
#include <cstddef>

typedef void (*uClockVoidCallback)();
typedef void (*uClockTickCallback)(uint32_t tick);

class uClockClass
{
public:
    enum PPQNResolution
    {
        PPQN_1 = 1,
        PPQN_2 = 2,
        PPQN_4 = 4,
        PPQN_8 = 8,
        PPQN_12 = 12,
        PPQN_16 = 16,
        PPQN_24 = 24,
        PPQN_32 = 32,
        PPQN_48 = 48,
        PPQN_96 = 96,
        PPQN_192 = 192,
        PPQN_384 = 384,
        PPQN_480 = 480
    };
    enum ClockMode { INTERNAL_CLOCK, EXTERNAL_CLOCK };
    enum ClockState { PAUSED, STARTING, STARTED };

    void init();
    void start();
    void stop();
    void pause();
    void setTempo(float bpm);
    float getTempo();
    void setOutputPPQN(PPQNResolution resolution);
    void setInputPPQN(PPQNResolution resolution);
    void setClockMode(ClockMode mode);
    ClockMode getClockMode();
    uint32_t bpmToMicroSeconds(float bpm);

    void setShuffle(bool active);
    bool isShuffled();
    void setShuffleSize(uint8_t size);
    void setShuffleData(uint8_t step, int8_t tick);
    void setShuffleTemplate(int8_t *shuff, uint8_t size);
    int8_t getShuffleLength();

    void setOnStep(uClockTickCallback callback);
    void setOnOutputPPQN(uClockTickCallback callback);
    void setOnClockStart(uClockVoidCallback callback);
    void setOnClockStop(uClockVoidCallback callback);
};

extern uClockClass uClock;
