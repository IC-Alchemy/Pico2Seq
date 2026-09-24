#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "voice/Voice.h"
#include "voice/VoiceManager.h"
#include <rpdsp/dynamics.h>
#include <array>
#include <cmath>

using namespace Catch::Matchers;

// Master-bus coverage for the VoiceManager macro knob (Shift + master-volume
// fader morphs the last-DSP-before-the-DAC compressor along Warm/Glue/Punch).
// Gain reduction on high-amplitude streams is verified through the real mix
// path; the morph curve itself is pure and pinned to the specified anchors.

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

// Hot 4-voice mix, settled for 1 s at the given macro position.
float settledHotPeak(float macro)
{
    VoiceManager manager(4);
    std::array<uint8_t, 4> ids{};
    for (uint8_t i = 0; i < 4; ++i)
        ids[i] = manager.addVoice(hotConfig());
    manager.init(48000.0f);
    manager.setMasterMacro(macro);
    for (uint8_t i = 0; i < 4; ++i)
        manager.updateVoiceState(ids[i], hotNote(24.0f + i * 4.0f));
    for (int i = 0; i < 48000 + 64; ++i)
        manager.processAllVoices(); // drain queue, settle macro + detector
    return peakOver(manager, 4800);
}

void applyMacroTo(rpdsp::Compressor &comp, float macro)
{
    const auto s = VoiceManager::settingsForMacro(macro);
    comp.prepare(48000.0f);
    comp.setThresholdDb(s.thresholdDb);
    comp.setRatio(s.ratio);
    comp.setKneeWidthDb(s.kneeDb);
    comp.setAttackRelease(s.attackMs, s.releaseMs);
    comp.setMakeupGainDb(s.makeupDb);
    comp.reset();
}
} // namespace

TEST_CASE("Master macro anchors match the specified Warm/Glue/Punch curve", "[master][compressor]")
{
    // Exact anchors from the macro spec (piecewise-linear between them).
    const auto warm = VoiceManager::settingsForMacro(0.0f);
    REQUIRE_THAT(warm.thresholdDb, WithinAbs(-14.0f, 1e-6f));
    REQUIRE_THAT(warm.ratio, WithinAbs(2.5f, 1e-6f));
    REQUIRE_THAT(warm.kneeDb, WithinAbs(9.0f, 1e-6f));
    REQUIRE_THAT(warm.attackMs, WithinAbs(30.0f, 1e-6f));
    REQUIRE_THAT(warm.releaseMs, WithinAbs(250.0f, 1e-6f));
    REQUIRE_THAT(warm.makeupDb, WithinAbs(2.0f, 1e-6f));

    const auto center = VoiceManager::settingsForMacro(0.5f);
    REQUIRE_THAT(center.thresholdDb, WithinAbs(-10.0f, 1e-6f));
    REQUIRE_THAT(center.ratio, WithinAbs(1.8f, 1e-6f));
    REQUIRE_THAT(center.kneeDb, WithinAbs(6.0f, 1e-6f));
    REQUIRE_THAT(center.makeupDb, WithinAbs(1.5f, 1e-6f));

    const auto punch = VoiceManager::settingsForMacro(1.0f);
    REQUIRE_THAT(punch.thresholdDb, WithinAbs(-14.0f, 1e-6f));
    REQUIRE_THAT(punch.ratio, WithinAbs(6.0f, 1e-6f));
    REQUIRE_THAT(punch.kneeDb, WithinAbs(2.0f, 1e-6f));
    REQUIRE_THAT(punch.attackMs, WithinAbs(6.0f, 1e-6f));
    REQUIRE_THAT(punch.releaseMs, WithinAbs(70.0f, 1e-6f));
    REQUIRE_THAT(punch.makeupDb, WithinAbs(4.0f, 1e-6f));

    // Mid-segment points lerp exactly between their anchors.
    const auto quarter = VoiceManager::settingsForMacro(0.25f);
    REQUIRE_THAT(quarter.attackMs, WithinAbs(22.5f, 1e-5f));
    REQUIRE_THAT(quarter.ratio, WithinAbs(2.15f, 1e-5f));
    const auto threeQuarter = VoiceManager::settingsForMacro(0.75f);
    REQUIRE_THAT(threeQuarter.attackMs, WithinAbs(10.5f, 1e-5f));
    REQUIRE_THAT(threeQuarter.ratio, WithinAbs(3.9f, 1e-5f));

    // Out-of-range clamps to the ends.
    const auto below = VoiceManager::settingsForMacro(-1.0f);
    REQUIRE_THAT(below.attackMs, WithinAbs(warm.attackMs, 1e-6f));
    const auto above = VoiceManager::settingsForMacro(2.0f);
    REQUIRE_THAT(above.ratio, WithinAbs(punch.ratio, 1e-6f));

    // setMasterMacro clamps the same way.
    VoiceManager manager(1);
    manager.setMasterMacro(-1.0f);
    REQUIRE(manager.getMasterMacro() == 0.0f);
    manager.setMasterMacro(2.0f);
    REQUIRE(manager.getMasterMacro() == 1.0f);
    REQUIRE(VoiceManager().getMasterMacro() == VoiceManager::kMacroDefault);
}

TEST_CASE("Master macro curve limits hot inputs and passes silence", "[master][compressor]")
{
    // Reference compressor built from the center of the manager's own curve:
    // the mix path must behave as a limiter, not a static scaler or a gate.
    rpdsp::Compressor comp;
    applyMacroTo(comp, 0.5f);

    REQUIRE(comp.process(0.0f) == 0.0f); // silence stays silent

    for (int i = 0; i < 48000; ++i)
        comp.process(0.9f); // settle detector + gain smoother past the attack
    float peak = 0.0f;
    for (int i = 0; i < 4800; ++i)
        peak = std::max(peak, std::abs(comp.process(0.9f)));
    REQUIRE(peak < 0.9f);  // settled below the hot input: limiting
    REQUIRE(peak > 0.3f);  // ...but the signal survives: glue, not gating
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

TEST_CASE("Punch end of the macro squeezes harder than Warm", "[master][compressor]")
{
    const float warmPeak = settledHotPeak(0.0f);
    const float punchPeak = settledHotPeak(1.0f);
    REQUIRE(std::isfinite(warmPeak));
    REQUIRE(std::isfinite(punchPeak));
    REQUIRE(punchPeak < warmPeak); // 6:1 smashes harder than 2.5:1 leveling
    REQUIRE(punchPeak <= 1.0f);    // ...down to DAC-safe levels
    REQUIRE(punchPeak > 0.01f);    // ...while makeup keeps it alive
}

TEST_CASE("Macro moves morph the mix gradually, not instantly", "[master][compressor]")
{
    // Two identical managers run in lockstep so their voice beat phases
    // align at every sample; only the macro motion differs between them.
    VoiceManager warm(4), morphing(4);
    std::array<uint8_t, 4> warmIds{}, morphIds{};
    for (uint8_t i = 0; i < 4; ++i)
    {
        warmIds[i] = warm.addVoice(hotConfig());
        morphIds[i] = morphing.addVoice(hotConfig());
    }
    warm.init(48000.0f);
    morphing.init(48000.0f);
    morphing.setMasterMacro(0.0f);
    warm.setMasterMacro(0.0f);
    for (uint8_t i = 0; i < 4; ++i)
    {
        warm.updateVoiceState(warmIds[i], hotNote(24.0f + i * 4.0f));
        morphing.updateVoiceState(morphIds[i], hotNote(24.0f + i * 4.0f));
    }
    for (int i = 0; i < 48000 + 64; ++i)
    {
        warm.processAllVoices();
        morphing.processAllVoices();
    }

    // Throw the knob fully across on one manager: the first milliseconds
    // still read Warm (eased morph + detector attack), seconds later Punch.
    morphing.setMasterMacro(1.0f);
    float warmEarly = 0.0f, morphEarly = 0.0f;
    for (unsigned i = 0; i < 480; ++i)
    {
        warmEarly = std::max(warmEarly, std::abs(warm.processAllVoices()));
        morphEarly = std::max(morphEarly, std::abs(morphing.processAllVoices()));
    }
    REQUIRE(std::isfinite(warmEarly));
    REQUIRE(std::isfinite(morphEarly));
    REQUIRE_THAT(morphEarly, WithinAbs(warmEarly, warmEarly * 0.25f));

    for (int i = 0; i < 48000; ++i)
    {
        warm.processAllVoices();
        morphing.processAllVoices();
    }
    const float warmLate = peakOver(warm, 4800);
    const float morphLate = peakOver(morphing, 4800);
    REQUIRE(std::isfinite(warmLate));
    REQUIRE(std::isfinite(morphLate));
    REQUIRE(morphLate < warmLate);
}
