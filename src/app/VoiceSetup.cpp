#include "VoiceSetup.h"
#include "AppState.h"
#include "AudioEngine.h"
#include "../sitar/SitarPerformance.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceEditParameters.h"
#include <Arduino.h>

// One-shot construction: presets → configs → sequencer links → lane maps.
// Publishing happens later in Application::begin(), after the rest of setup.

void initializeVoices()
{
    // Four factory voices from the saved (or default) preset picks.
    voiceManager = std::make_unique<VoiceManager>(VoiceSystem::MAX_VOICES);

    // Preset picks from UIState (saved song or defaults).
    const uint8_t *presetIndices = uiState.voicePresetIndices;

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        VoiceConfig config=VoicePresets::getPresetConfig(presetIndices[i]);
        // Link each voice to its sequencer, then give lanes their patch-relative maps.
        VoiceEdit::enablePatch(config);
        voiceSystem.setVoiceId(i, voiceManager->addVoice(config));
        voiceManager->attachSequencer(voiceSystem.getVoiceId(i), AppState::sequencers[i]);
        AppState::sequencers[i]->setPlaybackTransform(VoiceEdit::composeLane,
            voiceManager->getVoiceConfig(voiceSystem.getVoiceId(i)),VoiceEdit::mapOctave);
        VoiceEdit::seedModifiers(*AppState::sequencers[i]);
    }

    // Sitar Explorer's courses are prepared here (Core 0, before Core 1 starts
    // rendering) and hung on the master bus, so the mode's instrument is ready
    // the moment it is entered.
    Sitar::Performance::begin();

    // Publishing happens later in Application::begin(), after the rest of setup.
}

void applyVoicePreset(uint8_t voiceIndex, uint8_t presetIndex)
{
    if (presetIndex >= VoicePresets::getPresetCount())
    {
        Serial.println("Invalid preset index");
        return;
    }

    if (voiceIndex >= VoiceSystem::MAX_VOICES)
    {
        Serial.println("Invalid voice index");
        return;
    }

    VoiceConfig config = VoicePresets::getPresetConfig(presetIndex);
    VoiceEdit::enablePatch(config);
    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);

    if (voiceManager->setVoiceConfig(voiceId, config))
    {
        voiceManager->setVoiceSlide(voiceId,config.slideSeconds);
        uiState.voiceEditor.changed[voiceIndex]=false;
        Serial.print("Applied preset '");
        Serial.print(VoicePresets::getPresetName(presetIndex));
        Serial.print("' to Voice ");
        Serial.println(voiceIndex); // 0-based; UI shows 1-based
    }
    else
    {
        Serial.println("Failed to apply voice preset");
    }
}
