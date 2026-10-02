#include "VoiceDump.h"
#include "AppState.h"
#include "../voice/VoiceManager.h"
#include "../voice/VoiceSystem.h"
#include <Arduino.h>

void printAllVoiceValues()
{
    Serial.println("[VOICES] ---- current voice values ----");
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
    {
        const VoiceState &s = voiceSystem.getVoiceState(i);
        // Voice index is 0-based internally; shown 1-based to match the panel.
        Serial.printf("[VOICES] V%u%s gate=%u note=%.2f oct=%d vel=%.3f filt=%.3f "
                      "A=%.3f D=%.3f S=%.3f R=%.3f len=%u slide=%u\n",
                      static_cast<unsigned>(i + 1),
                      i == uiState.selectedVoiceIndex ? "*" : "",
                      s.isGateHigh ? 1u : 0u, s.noteIndex, static_cast<int>(s.octaveOffset),
                      s.velocityLevel, s.filterCutoff, s.attackTimeSeconds, s.decayTimeSeconds,
                      s.sustainLevel, s.releaseTimeSeconds,
                      static_cast<unsigned>(s.gateLengthTicks), s.hasSlide ? 1u : 0u);
        if (!voiceManager)
            continue;
        const VoiceConfig *c = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(i));
        if (!c)
            continue;
        Serial.printf("[VOICES]    patch engine=%u baseNote=%.2f baseVel=%.3f baseOct=%.1f "
                      "baseGateLen=%.3f res=%.3f cutoffBase=%.3f out=%.3f\n",
                      static_cast<unsigned>(c->engine), c->baseNote, c->baseVelocity,
                      c->baseOctave, c->baseGateLength, c->filterRes, c->filterCutoffBase,
                      c->outputLevel);
    }
}
