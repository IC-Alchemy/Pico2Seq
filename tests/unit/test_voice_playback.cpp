#include <catch2/catch_test_macros.hpp>
#include "app/AppState.h"
#include "app/VoicePlayback.h"
#include "ui/UITransitions.h"

namespace {
struct PlaybackFixture {
    VoiceState lastRequested[VoiceSystem::MAX_VOICES]{};
    unsigned updates[VoiceSystem::MAX_VOICES]{};
    PlaybackFixture() {
        voiceSystem = VoiceSystem{};
        voiceManager = std::make_unique<VoiceManager>(VoiceSystem::MAX_VOICES);
        // One callback records every published update: VoiceManager supports
        // a single callback, so tests must not replace it.
        voiceManager->setVoiceUpdateCallback([this](uint8_t id, const VoiceState &s) {
            for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
                if (id == voiceSystem.getVoiceId(i)) {
                    lastRequested[i] = s;
                    ++updates[i];
                }
        });
        for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
            *AppState::sequencers[i] = Sequencer{};
            voiceSystem.setVoiceId(i, voiceManager->addVoice(VoiceConfig{}));
        }
        voiceManager->init(48000);
    }
    ~PlaybackFixture() { voiceManager.reset(); }

    void play(uint8_t index, uint16_t ticks, bool slide = false) {
        auto &seq = *AppState::sequencers[index];
        seq.start();
        seq.setStepParameterValue(ParamId::Gate, 0, 1.0f);
        seq.setStepParameterValue(ParamId::Note, 0, index + 5.0f);
        seq.setStepParameterValue(ParamId::GateLength, 0,
            static_cast<float>(ticks) / SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS);
        seq.setStepParameterValue(ParamId::Slide, 0, slide ? 1.0f : 0.0f);
        VoiceState state = voiceSystem.getVoiceState(index);
        seq.playStepNow(0, &state);
        publishVoiceState(index, state);
    }
    // getRequestedState() clears a queued retrigger (the event belongs to the
    // published copy), so read retrigger events from the update callback.
    const VoiceState &requested(uint8_t index) { return lastRequested[index]; }
};
}

TEST_CASE("Published retriggers are events, not retained edit state", "[voice_playback]") {
    PlaybackFixture fixture;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
        fixture.play(i, 60);
        REQUIRE(fixture.requested(i).isGateHigh);
        REQUIRE(fixture.requested(i).shouldRetrigger);
        REQUIRE_FALSE(voiceSystem.getVoiceState(i).shouldRetrigger);
        // An in-place live edit must publish without repeating the step event.
        auto &state = voiceSystem.getVoiceState(i);
        AppState::sequencers[i]->refreshVoiceParameters(&state);
        publishVoiceState(i, state);
        REQUIRE_FALSE(fixture.requested(i).shouldRetrigger);
        REQUIRE(fixture.requested(i).isGateHigh);
    }
    const auto before = fixture.requested(0).noteIndex;
    VoiceState invalid{};
    invalid.noteIndex = 99;
    publishVoiceState(255, invalid);
    REQUIRE(fixture.requested(0).noteIndex == before);
}

TEST_CASE("Each sequencer publishes gate expiry once on its own tick", "[voice_playback]") {
    PlaybackFixture fixture;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i)
        fixture.play(i, 30 * (i + 1));
    for (unsigned tick = 1; tick <= 121; ++tick) {
        tickSequencerVoices();
        for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
            CAPTURE(i, tick, fixture.updates[i], fixture.requested(i).isGateHigh,
                    voiceSystem.getVoiceState(i).isGateHigh,
                    fixture.requested(i).gateLengthTicks);
            const bool high = tick < 30u * (i + 1);
            CHECK(fixture.requested(i).isGateHigh == high);
            CHECK(voiceSystem.getVoiceState(i).isGateHigh == high);
            CHECK(fixture.updates[i] == (high ? 1u : 2u));
        }
    }
}

TEST_CASE("Stop clears all note lifecycles and later ticks cannot republish", "[voice_playback]") {
    PlaybackFixture fixture;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) fixture.play(i, 60, true);
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
        stopSequencerVoice(i);
        REQUIRE_FALSE(AppState::sequencers[i]->isRunning());
        REQUIRE_FALSE(AppState::sequencers[i]->isNotePlaying());
        // The stop publication itself clears the gate in the requested state.
        CHECK_FALSE(fixture.requested(i).isGateHigh);
        CHECK_FALSE(fixture.requested(i).shouldRetrigger);
    }
    unsigned stopTotal = 0;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) stopTotal += fixture.updates[i];
    for (int tick = 0; tick < 120; ++tick) tickSequencerVoices();
    unsigned afterTotal = 0;
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) afterTotal += fixture.updates[i];
    REQUIRE(afterTotal == stopTotal); // no further updates after stop
    for (uint8_t i = 0; i < VoiceSystem::MAX_VOICES; ++i) {
        fixture.play(i, 60);
        CHECK(fixture.requested(i).isGateHigh);
        CHECK(fixture.requested(i).shouldRetrigger);
    }
}

TEST_CASE("Voice focus does not end sounding notes and slide does not retrigger", "[voice_playback]") {
    PlaybackFixture fixture;
    UIState ui;
    fixture.play(3, 60, true);
    REQUIRE(fixture.requested(3).shouldRetrigger);
    REQUIRE(UITransitions::selectPerformanceVoice(ui, 0));
    UITransitions::focusPad(ui, 1, 5);
    CHECK(fixture.requested(3).isGateHigh);
    CHECK(AppState::sequencers[3]->isNotePlaying());
    fixture.play(3, 60, true);
    CHECK(fixture.requested(3).isGateHigh);
    CHECK_FALSE(fixture.requested(3).shouldRetrigger);
}
