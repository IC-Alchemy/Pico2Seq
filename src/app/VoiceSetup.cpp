#include "VoiceSetup.h"
#include "AppState.h"
#include "AudioEngine.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceEditParameters.h"
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
        VoiceConfig config=VoicePresets::getPresetConfig(presetIndices[i]);
        VoiceEdit::enablePatch(config);
        voiceSystem.setVoiceId(i, voiceManager->addVoice(config));
        voiceManager->attachSequencer(voiceSystem.getVoiceId(i), AppState::sequencers[i]);
        AppState::sequencers[i]->setPlaybackTransform(VoiceEdit::composeLane,
            voiceManager->getVoiceConfig(voiceSystem.getVoiceId(i)),VoiceEdit::mapOctave);
        VoiceEdit::seedModifiers(*AppState::sequencers[i]);
    }

    // Application publishes the collection after the rest of control setup.
}

static void seedRepurposedParamTracks(uint8_t voiceIndex, const VoiceConfig &config)
{
    // Preset changes replace bases, never the recorded modifiers.
    (void)voiceIndex; (void)config;
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
        seedRepurposedParamTracks(voiceIndex, config);
        voiceManager->setVoiceSlide(voiceId,config.slideSeconds);
        uiState.voiceEditor.changed[voiceIndex]=false;
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
