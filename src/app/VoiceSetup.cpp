#include "VoiceSetup.h"
#include "AppState.h"
#include "AudioEngine.h"
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

    // The looper's 12-bit buffer comes LAST, from whatever heap the voices, delay and reverb
    // left, keeping a reserve for the audio pool and flash I/O. No buffer just means no
    // looper: the bus is unaffected.
    const int freeHeap = rp2040.getFreeHeap();
    const bool looper = voiceManager->allocateLoopBuffer(freeHeap > 0 ? static_cast<size_t>(freeHeap) : 0);
    loopController.bind(&voiceManager->loop(), 48000.0f);
    if (looper)
        Serial.printf("[LOOP] buffer %u bytes = %u samples (heap free %d)\n",
                      static_cast<unsigned>(voiceManager->loopBufferBytes()),
                      static_cast<unsigned>(voiceManager->loop().capacitySamples()), freeHeap);
    else
        Serial.printf("[LOOP] disabled: heap free %d is under the %u byte reserve plus a usable buffer\n",
                      freeHeap, static_cast<unsigned>(LoopEngine::kHeapReserveBytes));

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
