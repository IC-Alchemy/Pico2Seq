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

// Session capture/apply in three stages: before voices (preset picks), after voices
// (patterns+patches), after clock (tempo/feel). Staging matters — voices must exist
// before their patterns can land. Core 0 only.

bool Session::g_bootLoadedOk = false;

namespace
{
Session::PendingAction g_pending = Session::PendingAction::None; // Core 0 single-writer handoff
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

void Session::captureSession(persistence::ProjectSnapshot &out)
{
    out = persistence::ProjectSnapshot{}; // Clear first: changedFlags OR-accumulates below
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        persistence::capturePattern(*AppState::sequencers[v], out.patterns[v], out.envelopes[v]);
        const VoiceConfig *config =
            voiceManager ? voiceManager->getVoiceConfig(voiceSystem.getVoiceId(v)) : nullptr;
        if (config)
        {
            voicecodec::capturePatch(*config, out.patches[v]);
            // Sitar tails: the ten ENGINE_SITAR fields live in the snapshot's
            // format-3 tails, outside the size-locked PatchSnapshot.
            persistence::SitarPatchSnapshot &sitar = out.sitar[v];
            sitar.decay = config->sitarDecay;
            sitar.brightness = config->sitarBrightness;
            sitar.pickPosition = config->sitarPickPosition;
            sitar.pickHardness = config->sitarPickHardness;
            sitar.jawari = config->sitarJawari;
            sitar.jawariThreshold = config->sitarJawariThreshold;
            sitar.tarafAmount = config->sitarTarafAmount;
            sitar.tarafDecay = config->sitarTarafDecay;
            sitar.bodyAmount = config->sitarBodyAmount;
            sitar.bodyFrequency = config->sitarBodyFrequency;
        }
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
    out.laneModel = persistence::LANE_MODEL_ABSOLUTE;
}

void Session::applyBeforeVoices(const persistence::ProjectSnapshot &s)
{
    // Factory voices are built from these picks; the rest applies once they exist.
    const uint8_t presetCount = VoicePresets::getPresetCount();
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        uiState.voicePresetIndices[v] = (s.settings.presetIndices[v] < presetCount)
                                            ? s.settings.presetIndices[v]
                                            : uiState.voicePresetIndices[v];
    }
}

void Session::applyAfterVoices(persistence::ProjectSnapshot &s)
{
    uint8_t cappedTracks = 0;
    for (uint8_t v = 0; v < VoiceSystem::MAX_VOICES; ++v)
    {
        VoiceConfig config;
        if (voicecodec::applyPatch(uiState.voicePresetIndices[v], s.patches[v], config))
        {
            // Restore the saved sitar tuning (zeros for v1/v2 files, whose
            // patches predate ENGINE_SITAR — a non-sitar engine never reads
            // them, and the first Sitar preset apply overwrites them).
            const persistence::SitarPatchSnapshot &sitar = s.sitar[v];
            config.sitarDecay = sitar.decay;
            config.sitarBrightness = sitar.brightness;
            config.sitarPickPosition = sitar.pickPosition;
            config.sitarPickHardness = sitar.pickHardness;
            config.sitarJawari = sitar.jawari;
            config.sitarJawariThreshold = sitar.jawariThreshold;
            config.sitarTarafAmount = sitar.tarafAmount;
            config.sitarTarafDecay = sitar.tarafDecay;
            config.sitarBodyAmount = sitar.bodyAmount;
            config.sitarBodyFrequency = sitar.bodyFrequency;
            VoiceEdit::enablePatch(config);
            const uint8_t voiceId = voiceSystem.getVoiceId(v);
            voiceManager->setVoiceConfig(voiceId, config);
            voiceManager->setVoiceSlide(voiceId, config.slideSeconds);
        }
        // Format-1 lanes were patch-relative offsets: resolve to the values heard.
        const VoiceConfig *applied = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(v));
        if (s.laneModel == persistence::LANE_MODEL_OFFSETS && applied)
        {
            for (const ParamId lane : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay})
            {
                auto &track = s.patterns[v].tracks[static_cast<uint8_t>(lane)];
                VoiceEdit::convertOffsetValues(lane, track.values, SequencerConstants::MAX_STEPS_COUNT, *applied);
            }
        }

        // Pads/LEDs/OLED show 16 steps: longer lanes would play invisible steps.
        persistence::applyPattern(s.patterns[v], s.envelopes[v], *AppState::sequencers[v],
                                  NUMBER_OF_STEP_BUTTONS);
        for (const auto &track : s.patterns[v].tracks)
        {
            if (track.stepCount > NUMBER_OF_STEP_BUTTONS)
                ++cappedTracks;
        }
        for (const auto &track : s.envelopes[v].tracks)
        {
            if (track.stepCount > NUMBER_OF_STEP_BUTTONS)
                ++cappedTracks;
        }
        uiState.voiceEditor.cursor[v] = static_cast<VoiceEdit::Id>(s.settings.editorCursor[v]);
        uiState.voiceEditor.changed[v] = (s.settings.changedFlags & (1u << v)) != 0;
    }
    if (cappedTracks > 0)
        Serial.printf("[STORAGE] capped %u saved track lengths to %u steps\n",
                      static_cast<unsigned>(cappedTracks), static_cast<unsigned>(NUMBER_OF_STEP_BUTTONS));
    s.laneModel = persistence::LANE_MODEL_ABSOLUTE; // converted above
    // Focus restore only — not a live voice press, so no note cleanup or notice.
    uiState.selectedVoiceIndex = s.settings.selectedVoice;
    uiState.slideMode = (s.settings.changedFlags & 0x10u) != 0;
    if (voiceManager)
        voiceManager->setGlobalVolume(s.settings.masterVolume);
}

void Session::applyAfterClock(const persistence::ProjectSnapshot &s)
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
