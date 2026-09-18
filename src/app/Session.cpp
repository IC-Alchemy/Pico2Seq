#include "Session.h"
#include "AppState.h"
#include "../pico2seq-core/sequencer/ShuffleTemplates.h"
#include "../ui/UIConstants.h"
#include "../ui/UIState.h"
#include "../LEDMatrix/LEDMatrixFeedback.h"
#include "../voice/PatchCodec.h"
#include "../voice/VoiceEditParameters.h"
#include "../voice/VoicePresets.h"
#include "../voice/VoiceSystem.h"
#include "../pico2seq-core/persistence/PatternCodec.h"
#include <Arduino.h>
#include <uClock.h>

bool Session::g_bootLoadedOk = false;

namespace
{
Session::PendingAction g_pending = Session::PendingAction::None; // Core-0 single-writer flag
uint32_t g_lastSavedCrc = 0;
} // namespace

void Session::requestSave() { g_pending = Session::PendingAction::Save; }
void Session::requestLoad() { g_pending = Session::PendingAction::Load; }
Session::PendingAction Session::consumePendingAction()
{
    const Session::PendingAction action = g_pending;
    g_pending = Session::PendingAction::None;
    return action;
}
uint32_t Session::lastSavedCrc() { return g_lastSavedCrc; }
void Session::setLastSavedCrc(uint32_t crc) { g_lastSavedCrc = crc; }

void Session::captureSession(persistence::ProjectSnapshotV1 &out)
{
    out = persistence::ProjectSnapshotV1{}; // changedFlags uses read-modify-write below
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        persistence::capturePattern(*AppState::sequencers[v], out.patterns[v]);
        const VoiceConfig *config =
            voiceManager ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(v)) : nullptr;
        if (config)
            voicecodec::capturePatch(*config, out.patches[v]);
        out.patches[v].presetIndex = uiState.voicePresetIndices[v];

        out.settings.editorCursor[v] = static_cast<uint8_t>(uiState.voiceEditor.cursor[v]);
        if (uiState.voiceEditor.changed[v])
            out.settings.changedFlags |= static_cast<uint8_t>(1u << v);
    }
    out.settings.tempoBpm = uClock.getTempo();
    out.settings.masterVolume = voiceManager ? voiceManager->getGlobalVolume() : 0.8f;
    out.settings.themeIndex = uiState.currentThemeIndex;
    out.settings.currentScale = currentScale;
    out.settings.shuffleIndex = uiState.currentShufflePatternIndex;
    out.settings.selectedVoice = uiState.selectedVoiceIndex;
    if (uiState.slideMode)
        out.settings.changedFlags |= 0x10u;
}

void Session::applyBeforeVoices(const persistence::ProjectSnapshotV1 &s)
{
    // initializeVoices() reads voicePresetIndices to build the factory voices;
    // everything else applies after those voices exist.
    const uint8_t presetCount = VoicePresets::getPresetCount();
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        uiState.voicePresetIndices[v] = (s.settings.presetIndices[v] < presetCount)
                                            ? s.settings.presetIndices[v]
                                            : uiState.voicePresetIndices[v];
    }
}

void Session::applyAfterVoices(const persistence::ProjectSnapshotV1 &s)
{
    uint8_t cappedTracks = 0;
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        // Pads, LEDs and OLED show 16 steps per voice. A longer lane would play
        // steps nobody can see or edit, and its playhead would leave the grid.
        persistence::applyPattern(s.patterns[v], *AppState::sequencers[v], NUMBER_OF_STEP_BUTTONS);
        for (uint8_t t = 0; t < PARAM_ID_COUNT; ++t)
        {
            if (s.patterns[v].tracks[t].stepCount > NUMBER_OF_STEP_BUTTONS)
                ++cappedTracks;
        }

        VoiceConfig config;
        if (voicecodec::applyPatch(uiState.voicePresetIndices[v], s.patches[v], config))
        {
            VoiceEdit::enablePatch(config);
            const uint8_t voiceId = voiceSystem.getVoiceId(v);
            voiceManager->setVoiceConfig(voiceId, config);
            voiceManager->setVoiceSlide(voiceId, config.slideSeconds);
        }
        uiState.voiceEditor.cursor[v] = static_cast<VoiceEdit::Id>(s.settings.editorCursor[v]);
        uiState.voiceEditor.changed[v] = (s.settings.changedFlags & (1u << v)) != 0;
    }
    if (cappedTracks > 0)
        Serial.printf("[STORAGE] capped %u saved track lengths to %u steps\n",
                      static_cast<unsigned>(cappedTracks), static_cast<unsigned>(NUMBER_OF_STEP_BUTTONS));
    // Restore validated focus only; this is not a live performance voice press.
    uiState.selectedVoiceIndex = s.settings.selectedVoice;
    uiState.slideMode = (s.settings.changedFlags & 0x10u) != 0;
    if (voiceManager)
        voiceManager->setGlobalVolume(s.settings.masterVolume);
}

void Session::applyAfterClock(const persistence::ProjectSnapshotV1 &s)
{
    uClock.setTempo(s.settings.tempoBpm);
    // Same three lines as BUTTON_CHANGE_SWING_PATTERN (ButtonHandlers.cpp).
    const ShuffleTemplate &tmpl = shuffleTemplates[s.settings.shuffleIndex];
    uClock.setShuffleTemplate(const_cast<int8_t *>(tmpl.ticks), SHUFFLE_TEMPLATE_SIZE);
    uClock.setShuffle(s.settings.shuffleIndex > 0);
    uiState.currentShufflePatternIndex = s.settings.shuffleIndex;
    currentScale = s.settings.currentScale;
    uiState.currentThemeIndex = s.settings.themeIndex;
    setLEDTheme(static_cast<LEDTheme>(s.settings.themeIndex));
}
