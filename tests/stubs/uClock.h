#pragma once
#include <stdint.h>

// Host stub for the stock midilab uClock library (installed 2.2.1): only the
// names and signatures host-compiled firmware code calls. The tempo is a
// plain field so tests can drive it via uClock.setTempo().
namespace umodular {
namespace clock {

class uClockClass {
public:
    void start() {}
    void stop() {}
    void setTempo(float bpm) { tempo = bpm; }
    float getTempo() const { return tempo; }

    float tempo = 120.0f;
};

} // namespace clock
} // namespace umodular

inline umodular::clock::uClockClass uClock;
