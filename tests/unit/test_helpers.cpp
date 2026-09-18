#include <cstddef>
#include <cstdint>

// Definitions of external symbols required by Sequencer.cpp and Voice.cpp.
// Defined once here so individual test TUs don't conflict.

bool slideMode = false;
#include "app/AppState.h"

uint8_t currentScale = 0;
std::unique_ptr<VoiceManager> voiceManager;
VoiceSystem voiceSystem;
namespace AppState {
namespace {
Sequencer testSequencers[VoiceSystem::MAX_VOICES];
}
Sequencer *const sequencers[VoiceSystem::MAX_VOICES] = {
    &testSequencers[0], &testSequencers[1], &testSequencers[2], &testSequencers[3]};
}
