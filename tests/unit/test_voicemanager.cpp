#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "voice/VoiceManager.h"
#include "scales/scales.h"

using namespace Catch::Matchers;

TEST_CASE("VoiceManager: voice mix level defaults to 1.0", "[voice][voicemanager]") {
    VoiceManager vm(4);
    vm.init(48000.0f);

    uint8_t voiceId = vm.addVoice("analog");
    REQUIRE(voiceId != 0);

    // Default mix level must be 1.0f
    REQUIRE_THAT(vm.getVoiceMix(voiceId), WithinRel(1.0f, 1e-4f));

    // Verify const access works
    const VoiceManager& constVm = vm;
    REQUIRE_THAT(constVm.getVoiceMix(voiceId), WithinRel(1.0f, 1e-4f));
}

TEST_CASE("VoiceManager: setVoiceMix updates mix level", "[voice][voicemanager]") {
    VoiceManager vm(4);
    vm.init(48000.0f);

    uint8_t voiceId = vm.addVoice("analog");
    REQUIRE(voiceId != 0);

    vm.setVoiceMix(voiceId, 0.65f);
    REQUIRE_THAT(vm.getVoiceMix(voiceId), WithinRel(0.65f, 1e-4f));

    vm.setVoiceMix(voiceId, 0.0f);
    REQUIRE_THAT(vm.getVoiceMix(voiceId), WithinAbs(0.0f, 1e-6f));

    vm.setVoiceMix(voiceId, 1.5f);
    REQUIRE_THAT(vm.getVoiceMix(voiceId), WithinRel(1.5f, 1e-4f));
}

TEST_CASE("VoiceManager: getVoiceMix and setVoiceMix handle invalid voice ID gracefully", "[voice][voicemanager]") {
    VoiceManager vm(4);
    vm.init(48000.0f);

    uint8_t invalidId = 99;
    REQUIRE_THAT(vm.getVoiceMix(invalidId), WithinAbs(0.0f, 1e-6f));

    // Should not crash or modify other state
    REQUIRE_NOTHROW(vm.setVoiceMix(invalidId, 0.5f));
    REQUIRE_THAT(vm.getVoiceMix(invalidId), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("VoiceManager: setVoiceMix controls voice output on audio path", "[voice][voicemanager]") {
    VoiceManager vm(2);
    vm.init(48000.0f);
    vm.setGlobalVolume(1.0f);

    uint8_t voiceId = vm.addVoice("analog");
    REQUIRE(voiceId != 0);

    // Trigger note so voice produces non-zero audio
    VoiceState state;
    state.noteIndex = 12.0f;     // C
    state.velocityLevel = 1.0f;
    state.isGateHigh = true;
    vm.updateVoiceState(voiceId, state);

    // Run a few samples to let oscillator and envelope ramp up
    float activeSample = 0.0f;
    for (int i = 0; i < 64; ++i) {
        activeSample = vm.processVoice(voiceId);
    }
    // Verify voice is sounding
    REQUIRE(std::abs(activeSample) > 0.001f);

    // Mute via setVoiceMix(0.0f)
    vm.setVoiceMix(voiceId, 0.0f);
    float mutedSample = vm.processVoice(voiceId);
    REQUIRE_THAT(mutedSample, WithinAbs(0.0f, 1e-6f));

    // Test processAllVoices also respects mixLevel
    float allVoicesMuted = vm.processAllVoices();
    REQUIRE_THAT(allVoicesMuted, WithinAbs(0.0f, 1e-6f));

    // Restore mixLevel to 1.0f and check sound returns
    vm.setVoiceMix(voiceId, 1.0f);
    float restoredSample = vm.processVoice(voiceId);
    REQUIRE(std::abs(restoredSample) > 0.001f);
}

TEST_CASE("VoiceManager: multi-voice mix level balance", "[voice][voicemanager]") {
    VoiceManager vm(2);
    vm.init(48000.0f);
    vm.setGlobalVolume(1.0f);

    uint8_t v1 = vm.addVoice("analog");
    uint8_t v2 = vm.addVoice("analog");
    REQUIRE(v1 != 0);
    REQUIRE(v2 != 0);

    VoiceState state;
    state.noteIndex = 12.0f;
    state.velocityLevel = 1.0f;
    state.isGateHigh = true;
    vm.updateVoiceState(v1, state);
    vm.updateVoiceState(v2, state);

    // Mute v2, set v1 to 1.0
    vm.setVoiceMix(v1, 1.0f);
    vm.setVoiceMix(v2, 0.0f);

    // Prime oscillators
    for (int i = 0; i < 64; ++i) {
        vm.processAllVoices();
    }

    float outV1Solo = vm.processVoice(v1);
    float outV2Solo = vm.processVoice(v2);
    REQUIRE_THAT(outV2Solo, WithinAbs(0.0f, 1e-6f));
    REQUIRE(std::abs(outV1Solo) > 0.001f);

    // Mute v1, unmute v2
    vm.setVoiceMix(v1, 0.0f);
    vm.setVoiceMix(v2, 1.0f);

    outV1Solo = vm.processVoice(v1);
    outV2Solo = vm.processVoice(v2);
    REQUIRE_THAT(outV1Solo, WithinAbs(0.0f, 1e-6f));
    REQUIRE(std::abs(outV2Solo) > 0.001f);
}
