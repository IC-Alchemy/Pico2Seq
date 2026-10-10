#include "VoiceSetup.h"
#include "AppState.h"
#include "AudioEngine.h"
#include "UserPresetStorage.h"
#include "../voice/UserPresetCodec.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceEditParameters.h"
#include <Arduino.h>
#include <cstring>

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
        UserPresetStorage::setVoiceOrigin(uiState, voiceIndex, persistence::kNoSlot, nullptr);
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

bool applyUserPreset(uint8_t voiceIndex, uint8_t slot)
{
    if (voiceIndex >= VoiceSystem::MAX_VOICES || !voiceManager)
        return false;
    // One 256-byte record, read back from flash and checked again: the bank was verified when
    // it was loaded, but a pad tap should never hand the audio core an unchecked patch.
    static persistence::UserPresetRecord record;
    if (UserPresetStorage::store().readSlot(slot, record) != presetlink::UserPresetStore::Result::Ok ||
        !usercodec::validate(record).ok())
    {
        Serial.println("User preset unreadable");
        return false;
    }
    VoiceConfig config;
    if (!usercodec::toConfig(record, config))
        return false;
    VoiceEdit::enablePatch(config);
    const uint8_t voiceId = voiceSystem.getVoiceId(voiceIndex);
    if (!voiceManager->setVoiceConfig(voiceId, config))
    {
        Serial.println("Failed to apply user preset");
        return false;
    }
    voiceManager->setVoiceSlide(voiceId, config.slideSeconds);
    // voicePresetIndices keeps the factory base: the song file rebuilds the voice's lanes from it.
    uiState.voicePresetIndices[voiceIndex] = record.baseIndex;
    UserPresetStorage::setVoiceOrigin(uiState, voiceIndex, slot, record.name);
    uiState.voiceEditor.changed[voiceIndex] = false;
    Serial.print("Applied user preset '");
    Serial.print(record.name);
    Serial.print("' to Voice ");
    Serial.println(voiceIndex); // 0-based; UI shows 1-based
    return true;
}
