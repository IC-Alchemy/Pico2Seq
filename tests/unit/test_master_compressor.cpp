#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "voice/Voice.h"
#include "voice/VoiceManager.h"
#include <rpdsp/dynamics.h>
#include <array>
#include <cmath>

using namespace Catch::Matchers;

// Master-bus coverage for the VoiceManager glue + limiter (last DSP before
// the DAC). See docs/testing.md "Future Test Coverage": gain reduction on
// high-amplitude streams, verified through the real mix path.

namespace {
// Sustained, filterless sine voice: a steady hot signal for gain-reduction
// tests (no envelope shaping to confound attack/settle windows).
VoiceConfig hotConfig()
{
    VoiceConfig config;
    config.oscillatorCount = 1;
    config.oscWaveforms[0] = WAVE_SIN;
    config.hasEnvelope = false;
    config.hasFilter = false;
    return config;
}

VoiceState hotNote(float note)
{
    VoiceState state;
    state.noteIndex = note;
    state.velocityLevel = 1.0f;
    state.isGateHigh = true;
    state.shouldRetrigger = true;
    return state;
}

float peakOver(VoiceManager &manager, unsigned samples)
{
    float peak = 0.0f;
    for (unsigned i = 0; i < samples; ++i)
        peak = std::max(peak, std::abs(manager.processAllVoices()));
    return peak;
}
} // namespace

TEST_CASE("Master compressor settings limit hot inputs and pass silence", "[master][compressor]")
{
    // Reference compressor built from the manager's own constants: the mix
    // path must behave as a limiter, not a static scaler or a gate.
    rpdsp::Compressor comp;
    comp.prepare(48000.0f);
    comp.setThresholdDb(VoiceManager::kMasterCompThresholdDb);
    comp.setRatio(VoiceManager::kMasterCompRatio);
    comp.setKneeWidthDb(VoiceManager::kMasterCompKneeDb);
    comp.setAttackRelease(VoiceManager::kMasterCompAttackMs, VoiceManager::kMasterCompReleaseMs);
    comp.setMakeupGainDb(VoiceManager::kMasterCompMakeupDb);

    REQUIRE(comp.process(0.0f) == 0.0f); // silence stays silent

    for (int i = 0; i < 48000; ++i)
        comp.process(0.9f); // settle detector + gain smoother past the attack
    float peak = 0.0f;
    for (int i = 0; i < 4800; ++i)
        peak = std::max(peak, std::abs(comp.process(0.9f)));
    REQUIRE(peak < 0.9f);  // settled below the hot input: limiting
    REQUIRE(peak > 0.3f);  // ...but the signal survives: glue, not gating
}

TEST_CASE("Hot 4-voice mix is limited to DAC-safe levels", "[master][compressor]")
{
    VoiceManager manager(4);
    std::array<uint8_t, 4> ids{};
    for (uint8_t i = 0; i < 4; ++i)
        ids[i] = manager.addVoice(hotConfig());
    manager.init(48000.0f);
    for (uint8_t i = 0; i < 4; ++i)
        manager.updateVoiceState(ids[i], hotNote(24.0f + i * 4.0f));
    for (unsigned i = 0; i < 64; ++i)
        manager.processAllVoices(); // drain the control queue

    // Attack window: gain reduction is still opening, so this reads near the
    // uncompressed hot sum. Master gain starts settled (no volume moves).
    const float earlyPeak = peakOver(manager, 240);
    for (int i = 0; i < 48000; ++i)
        manager.processAllVoices(); // settle detector + gain smoother
    const float latePeak = peakOver(manager, 4800);

    REQUIRE(std::isfinite(earlyPeak));
    REQUIRE(std::isfinite(latePeak));
    REQUIRE(earlyPeak > latePeak * 1.1f); // the compressor rides the gain
    REQUIRE(latePeak <= 1.0f);            // ...down to DAC-safe levels
    REQUIRE(latePeak > 0.01f);            // ...while makeup keeps it alive
}

TEST_CASE("Silence stays silent through the master compressor", "[master][compressor]")
{
    VoiceManager manager(1);
    manager.init(48000.0f);
    std::array<float, 256> out{};
    out.fill(99.0f);
    manager.processBlock(out.data(), out.size());
    for (float s : out)
        REQUIRE(s == 0.0f);
}
