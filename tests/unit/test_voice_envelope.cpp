#include "app/AppState.h"
#include "app/VoiceEnvelope.h"
#include "ui/ControlSurfaceLogic.h"
#include "ui/UITransitions.h"
#include "voice/MusicalValues.h"
#include "voice/VoicePresets.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using Catch::Approx;

TEST_CASE("ADSR chord consumes modifiers, all voice shortcuts and release tails", "[voice_envelope]") {
    for (uint8_t voice = 0; voice < 4; ++voice) {
        VoiceEnvelope::Controls controls;
        CHECK_FALSE(controls.poll(128, 0).consumed); // Shift first
        CHECK(controls.poll(160, 0).consumed);       // then button 6
        CHECK_FALSE(controls.active);
        CHECK(controls.poll(160, 1u << voice).voice == voice);
        CHECK(controls.active);
        CHECK(controls.poll(160, 1u << voice).voice == -1); // no repeats
        CHECK(controls.poll(128, 1u << voice).consumed);
        CHECK(controls.poll(0, 0).consumed);
        CHECK_FALSE(controls.waitRelease);
        CHECK(controls.poll(0, 1u << ((voice + 1) % 4)).voice == (voice + 1) % 4);
        CHECK(controls.poll(128, 0).exit);
        CHECK_FALSE(controls.active);
        CHECK(controls.poll(128, 8).voice == -1); // exit cannot toggle arp/editor
        CHECK(controls.poll(0, 0).consumed);
        CHECK_FALSE(controls.poll(0, 0).consumed);
    }
    VoiceEnvelope::Controls canceled;
    CHECK(canceled.poll(160, 0).consumed);
    CHECK(canceled.poll(32, 0).consumed);
    const auto released = canceled.poll(0, 0);
    CHECK(released.consumed);
    CHECK(released.modifierTap);
    CHECK_FALSE(canceled.poll(0, 0).consumed);
    CHECK_FALSE(canceled.active);
}

TEST_CASE("ADSR entry clears competing UI state while leaving the arp running", "[voice_envelope]") {
    UIState ui;
    ui.arp.setActive(true);
    ui.arp.pressPad(3);
    ui.settingsMode = ui.slideMode = ui.gateSeqLengthMode = true;
    ui.parameterButtonHeld[0] = true;
    ui.selectedStepForEdit = 5;
    ui.padPressTimestamps[3] = 100;
    UITransitions::openVoiceEnvelope(ui, 2);
    CHECK(ui.voiceEnvelope.active);
    CHECK(ui.selectedVoiceIndex == 2);
    CHECK(ui.selectedStepForEdit == -1);
    CHECK_FALSE(ui.settingsMode);
    CHECK_FALSE(ui.slideMode);
    CHECK_FALSE(ui.gateSeqLengthMode);
    CHECK_FALSE(ui.parameterButtonHeld[0]);
    CHECK(ui.padPressTimestamps[3] == 0);
    CHECK(ui.arp.active());
    CHECK(ui.arp.padHeld(3));
}

namespace {
struct EnvelopeFixture {
    explicit EnvelopeFixture(const char *preset = "Digital") {
        uiState = {};
        voiceSystem = {};
        voiceManager = std::make_unique<VoiceManager>(4);
        auto config = VoicePresets::getPresetConfig(VoicePresets::findPreset(preset));
        VoiceEdit::enablePatch(config);
        for (uint8_t v = 0; v < 4; ++v) {
            const auto id = voiceManager->addVoice(config);
            voiceSystem.setVoiceId(v, id);
            auto &seq = *AppState::sequencers[v];
            seq = Sequencer{};
            seq.setPlaybackTransform(VoiceEdit::composeLane, voiceManager->getVoiceConfig(id), VoiceEdit::mapOctave);
            for (auto lane : {ParamId::Attack, ParamId::Decay, ParamId::Sustain, ParamId::Release})
                for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
                    seq.setStepParameterValue(lane, step, 0.2f);
            seq.setParameterStepCount(ParamId::Attack, 3);
        }
    }
    ~EnvelopeFixture() { voiceManager.reset(); uiState = {}; }
};
}

TEST_CASE("ADSR pickup leaves values alone until moved and resets on voice changes", "[voice_envelope]") {
    EnvelopeFixture fixture;
    ControlSurface::FaderMap faders;
    const auto id = voiceSystem.getVoiceId(1);
    const float before = voiceManager->getVoiceConfig(id)->defaultSustain;
    auto frame = [&](uint16_t raw) {
        if (faders.accept(2, raw))
            REQUIRE(VoiceEnvelope::set(1, 2, ControlSurface::FaderMap::normalize(faders.filtered(2)), false));
    };
    for (int i = 0; i < 10; ++i) frame(1200);
    frame(4095); // isolated noise spike cannot pick up
    frame(1200); frame(1200);
    CHECK(voiceManager->getVoiceConfig(id)->defaultSustain == before);
    frame(2200); frame(2200); frame(2200);
    CHECK(voiceManager->getVoiceConfig(id)->defaultSustain == Approx(2200.0f / 4095));
    faders.resetDeadband();
    for (int i = 0; i < 10; ++i) CHECK_FALSE(faders.accept(2, 2200));
}

TEST_CASE("Sequencer ADSR changes all stored steps of only the moved stage and voice", "[voice_envelope]") {
    EnvelopeFixture fixture;
    constexpr ParamId lanes[] = {ParamId::Attack, ParamId::Decay, ParamId::Sustain, ParamId::Release};
    for (uint8_t channel = 0; channel < 4; ++channel) {
        REQUIRE(VoiceEnvelope::set(2, channel, 0.75f, false));
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step) {
            CHECK(followsPatch(AppState::sequencers[2]->getStepParameterValue(lanes[channel], step)));
            CHECK(AppState::sequencers[1]->getStepParameterValue(lanes[channel], step) == 0.2f);
            for (uint8_t untouched = channel + 1; untouched < 4; ++untouched)
                CHECK(AppState::sequencers[2]->getStepParameterValue(lanes[untouched], step) == 0.2f);
        }
    }
    CHECK(AppState::sequencers[2]->getParameterStepCount(ParamId::Attack) == 3);
    const auto *config = voiceManager->getVoiceConfig(voiceSystem.getVoiceId(2));
    CHECK(config->defaultAttack == Approx(MusicalValues::attackSeconds(0.75f)));
    CHECK(config->defaultDecay == Approx(MusicalValues::envelopeSeconds(0.75f)));
    CHECK(config->defaultSustain == Approx(0.75f));
    CHECK(config->defaultRelease == Approx(MusicalValues::releaseSeconds(0.75f)));
}

TEST_CASE("Arp ADSR publishes immediately without rewriting steps or retriggering", "[voice_envelope]") {
    EnvelopeFixture fixture;
    for (bool gate : {false, true}) {
        auto &state = voiceSystem.getVoiceState(1);
        state.noteIndex = 19;
        state.octaveOffset = 12;
        state.velocityLevel = 0.37f;
        state.isGateHigh = gate;
        state.hasSlide = true;
        REQUIRE(VoiceEnvelope::set(1, 2, 0.9f, true));
        const auto *requested = voiceManager->getVoiceState(voiceSystem.getVoiceId(1));
        CHECK(requested->sustainLevel == Approx(0.9f));
        CHECK(requested->noteIndex == 19);
        CHECK(requested->octaveOffset == 12);
        CHECK(requested->velocityLevel == Approx(0.37f));
        CHECK(requested->isGateHigh == gate);
        CHECK(requested->hasSlide);
        CHECK_FALSE(requested->shouldRetrigger);
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            CHECK(AppState::sequencers[1]->getStepParameterValue(ParamId::Sustain, step) == 0.2f);
    }
}

TEST_CASE("Actual ADSR edits preserve repurposed timbre lanes", "[voice_envelope]") {
    for (const char *preset : {"WgPluck", "Hypersaw", "FMGlass"}) {
        INFO(preset);
        EnvelopeFixture fixture(preset);
        const auto id = voiceSystem.getVoiceId(0);
        const VoiceConfig before = *voiceManager->getVoiceConfig(id);
        REQUIRE_FALSE(VoiceParameters::layout(before).envelopeFromTracks);
        REQUIRE(VoiceEnvelope::set(0, 0, 0.9f, false));
        const auto *after = voiceManager->getVoiceConfig(id);
        CHECK(after->defaultAttack == Approx(MusicalValues::attackSeconds(0.9f)));
        CHECK(after->hasEnvelope == before.hasEnvelope);
        const auto &binding = VoiceParameters::binding(before, ParamId::Attack);
        if (binding.target) CHECK(after->*(binding.target) == before.*(binding.target));
        for (uint8_t step = 0; step < SequencerConstants::MAX_STEPS_COUNT; ++step)
            CHECK(AppState::sequencers[0]->getStepParameterValue(ParamId::Attack, step) == 0.2f);
    }
}

TEST_CASE("Live ADSR reaches a running sustain and release while ordinary edits still wait", "[voice_envelope][voice]") {
    VoiceConfig config;
    config.usePatchBases = true;
    config.hasFilter = config.hasOverdrive = false;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.oscAmplitudes[0] = 1.0f;
    Voice live(0, config), regular(0, config);
    live.init(48000); regular.init(48000);
    VoiceState state;
    state.isGateHigh = true;
    state.velocityLevel = 1.0f;
    state.attackTimeSeconds = state.decayTimeSeconds = 0.0f;
    state.sustainLevel = 0.2f;
    state.releaseTimeSeconds = 1.0f;
    live.updateParameters(state); regular.updateParameters(state);
    auto energy = [](Voice &v, int samples) {
        double sum = 0;
        for (int i = 0; i < samples; ++i) { const float x = v.process(); sum += x * x; }
        return sum;
    };
    energy(live, 4800); energy(regular, 4800);
    state.sustainLevel = 0.8f;
    live.updateParameters(state, 4); regular.updateParameters(state);
    CHECK(energy(live, 4800) > 4 * energy(regular, 4800));
    // A later ordinary update must not inherit the one-shot live mask.
    state.sustainLevel = 0.01f;
    live.updateParameters(state);
    CHECK(energy(live, 4800) > 4 * energy(regular, 4800));
    state.isGateHigh = false;
    live.updateParameters(state); regular.updateParameters(state);
    energy(live, 960); energy(regular, 960);
    state.releaseTimeSeconds = 0.0f;
    live.updateParameters(state, 8); regular.updateParameters(state);
    energy(live, 960);
    CHECK(energy(live, 4800) < energy(regular, 4800) * 0.01);
}
