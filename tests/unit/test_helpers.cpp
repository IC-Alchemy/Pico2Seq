#include <cstddef>

// Definitions of external symbols required by Sequencer.cpp and Voice.cpp.
// Defined once here so individual test TUs don't conflict.

bool slideMode = false;
const size_t MAX_DELAY_SAMPLES = 48000;
