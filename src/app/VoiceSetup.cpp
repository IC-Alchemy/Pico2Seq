#include "VoiceSetup.h"
#include "AppState.h"
#include "AudioEngine.h"
#include "../voice/VoicePresets.h"
#include <Arduino.h>

void initializeVoices()
{
    AudioEngine::prepareEffects();

    // Initialize Voice Manager with maximum 4 concurrent voices
    voiceManager = std::make_unique<VoiceManager>(VoiceSystem::MAX_VOICES);

    // Create voices 1-4 using presets from UIState defaults
    const uint8_t *presetIndices = uiState.voicePresetIndices;

    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; i++)
    {
        voiceSystem.setVoiceId(i, voiceManager->addVoice(VoicePresets::getPresetConfig(presetIndices[i])));
        voiceManager->attachSequencer(voiceSystem.getVoiceId(i), AppState::sequencers[i]);
        VoiceParameters::seedTracks(*AppState::sequencers[i], VoicePresets::getPresetConfig(presetIndices[i]));
    }

    // Application publishes the collection after the rest of control setup.
}

static void seedRepurposedParamTracks(uint8_t voiceIndex, const VoiceConfig &config)
{
    VoiceParameters::seedTracks(*AppState::sequencers[voiceIndex], config);
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
    uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);

    if (voiceManager->setVoiceConfig(voiceId, config))
    {
        seedRepurposedParamTracks(voiceIndex, config);
        Serial.print("Applied preset '");
        Serial.print(VoicePresets::getPresetName(presetIndex));
        Serial.print("' to Voice ");
        Serial.println(voiceIndex); // 0-based display
    }
    else
    {
        Serial.println("Failed to apply voice preset");
    }
}
