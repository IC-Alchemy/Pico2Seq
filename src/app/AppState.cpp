#include "AppState.h"

UIState uiState;
Sequencer seq1(1);
Sequencer seq2(2);
Sequencer seq3(3);
Sequencer seq4(4);
std::unique_ptr<VoiceManager> voiceManager;
std::atomic<bool> voicesReady{false};
VoiceSystem voiceSystem;
uint8_t currentScale = 0;
bool isClockRunning = true;

namespace AppState
{
Sequencer *const sequencers[VoiceSystem::MAX_VOICES] = {&seq1, &seq2, &seq3, &seq4};
PerformanceInput performanceInput;
}
