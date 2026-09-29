#pragma once
#include <cstdint>

namespace VoiceEnvelope {
// Core 0: change one actual ADSR stage, publish without retriggering, and in
// sequencer mode make every stored step of that envelope lane follow the patch.
bool set(uint8_t voice, uint8_t channel, float normalized, bool arpMode);
}
