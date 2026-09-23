// Sitar factory preset: registration in the preset bank, the owned
// JAWARI/PICK/TARAF layout derived from VoiceParameters::sitarLayout(), lane
// seeding into the right sitar controls, and the natural-decay chain toggles
// (hasEnvelope = false, no conventional synth filter). The lane mechanics are
// covered by test_sitar_lanes.cpp ([sitar_lanes]); the DSP model by
// test_sitar_voice.cpp ([rpdsp][sitar]); the engine wiring by
// test_sitar_engine.cpp ([sitar_engine]).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "voice/Voice.h"
#include "voice/VoiceParameters.h"
#include "voice/VoicePresets.h"
#include "voice/presets/SitarPresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using namespace Catch::Matchers;

namespace {
constexpr float kSampleRate = 48000.0f;

VoiceState seededState(const VoiceConfig &config)
{
    Sequencer seq;
    seq.initializeParameters();
    VoiceParameters::seedTracks(seq, config);
    VoiceState s;
    s.noteIndex = 9.0f;
    s.velocityLevel = 0.8f;
    s.filterCutoff = seq.getStepParameterValue(ParamId::Filter, 0);
    s.attackTimeSeconds = seq.getStepParameterValue(ParamId::Attack, 0);
    s.decayTimeSeconds = seq.getStepParameterValue(ParamId::Decay, 0);
    s.isGateHigh = true;
    return s;
}

double windowRms(const float *samples, size_t count)
{
    double energy = 0.0;
    for (size_t i = 0; i < count; ++i)
        energy += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(energy / static_cast<double>(count));
}
} // namespace

TEST_CASE("Sitar preset registers at the end of the preset bank",
          "[sitar_preset][presets]")
{
    const auto &c = VoicePresets::getSitarVoice();
    REQUIRE(VoicePresets::getPresetCount() ==
            static_cast<uint8_t>(VoicePresets::Id::Count));
    REQUIRE(VoicePresets::getPresetCount() == 30);
    REQUIRE(static_cast<uint8_t>(VoicePresets::Id::Sitar) == 29);
    REQUIRE(std::string(VoicePresets::getPresetName(29)) == "Sitar");
    REQUIRE(VoicePresets::findPreset("Sitar") == 29);
    REQUIRE(VoicePresets::findPreset("SITAR") == 29);
    REQUIRE(&VoicePresets::getPresetConfig(29) == &c);
    REQUIRE(&VoicePresets::getPresetConfigByName("sitar") == &c);
    REQUIRE(VoicePresets::getPresetParamSet(29) == PARAMSET_SITAR);
    // The registry stays append-only: every legacy position is unchanged.
    REQUIRE(std::string(VoicePresets::getPresetName(0)) == "Analog");
    REQUIRE(std::string(VoicePresets::getPresetName(9)) == "WgPluck");
    REQUIRE(std::string(VoicePresets::getPresetName(14)) == "NoiseStorm");
    REQUIRE(std::string(VoicePresets::getPresetName(28)) == "AirChime");
    // The browser pad for the new preset exists (pads 0-30; 31 unassigned).
    REQUIRE(VoicePresets::presetIndexForPad(29, VoicePresets::getPresetCount()) == 29);
}

TEST_CASE("Sitar preset declares the sitar engine and its character values",
          "[sitar_preset]")
{
    const auto &c = VoicePresets::getSitarVoice();
    REQUIRE(c.engine == ENGINE_SITAR);
    REQUIRE(c.paramSet == PARAMSET_SITAR);
    REQUIRE(c.parameters == &VoicePresets::kSitarLayout);
    REQUIRE(c.oscillatorCount == 0); // no synth oscillator bank
    REQUIRE(c.recipe == nullptr);

    // Bright hard pick, bridge-side pick position, moderate jawari.
    REQUIRE_THAT(c.sitarBrightness, WithinAbs(0.85f, 1e-6f));
    REQUIRE_THAT(c.sitarPickHardness, WithinAbs(0.9f, 1e-6f));
    REQUIRE_THAT(c.sitarPickPosition, WithinAbs(0.12f, 1e-6f));
    REQUIRE_THAT(c.sitarJawari, WithinAbs(0.45f, 1e-6f));
    REQUIRE_THAT(c.sitarJawariThreshold, WithinAbs(0.3f, 1e-6f));
    // Long-ish main-string decay (several seconds, not tens), strong but
    // quieter sympathetic bank.
    REQUIRE_THAT(c.sitarDecay, WithinAbs(4.0f, 1e-6f));
    REQUIRE_THAT(c.sitarTarafAmount, WithinAbs(0.45f, 1e-6f));
    REQUIRE_THAT(c.sitarTarafDecay, WithinAbs(4.0f, 1e-6f));
    // Light wooden body resonance.
    REQUIRE_THAT(c.sitarBodyAmount, WithinAbs(0.2f, 1e-6f));
    REQUIRE_THAT(c.sitarBodyFrequency, WithinAbs(130.0f, 1e-6f));

    // Natural physical-model decay; no conventional synth filter, no drive.
    REQUIRE_FALSE(c.hasEnvelope);
    REQUIRE_FALSE(c.hasFilter);
    REQUIRE_FALSE(c.hasOverdrive);
}

TEST_CASE("Sitar preset layout derives from sitarLayout with velocity in the pluck",
          "[sitar_preset]")
{
    const auto &c = VoicePresets::getSitarVoice();
    const auto &layout = VoiceParameters::layout(c);
    // Velocity lives in the pluck excitation (never double-applied in the
    // VCA) and the lanes stop driving the ADSR: the sitar rings naturally.
    REQUIRE_FALSE(layout.envelopeFromTracks);
    REQUIRE_FALSE(layout.velocityToAmplitude);
    REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(c));

    struct Lane
    {
        ParamId id;
        const char *name;
        float VoiceConfig::*target;
        VoiceParameters::Span span;
    };
    const Lane lanes[] = {
        {ParamId::Filter, "JAWARI", &VoiceConfig::sitarJawari, {0.05f, 0.45f, 0.95f}},
        {ParamId::Attack, "PICK", &VoiceConfig::sitarPickHardness, {0.3f, 0.9f, 1.0f}},
        {ParamId::Decay, "TARAF", &VoiceConfig::sitarTarafAmount, {0.0f, 0.45f, 0.9f}},
    };
    for (const auto &lane : lanes)
    {
        const auto &b = VoiceParameters::binding(c, lane.id);
        INFO(lane.name);
        REQUIRE(std::string(b.name) == lane.name);
        REQUIRE(b.target == lane.target);
        REQUIRE(b.seed);
        REQUIRE(b.minimum == lane.span.minimum);
        REQUIRE(b.center == lane.span.center);
        REQUIRE(b.maximum == lane.span.maximum);
        // The resting timbre sits on the lane midpoint: no jump on the first
        // sequencer step.
        REQUIRE_THAT(b.map(0.5f), WithinAbs(c.*(lane.target), 1e-6f));
    }
    // The other lanes keep their shared musical meaning.
    for (ParamId id : {ParamId::Note, ParamId::Velocity, ParamId::Octave,
                       ParamId::GateLength, ParamId::Gate, ParamId::Slide})
    {
        const auto &b = VoiceParameters::binding(c, id);
        INFO(static_cast<int>(id));
        REQUIRE(b.target == nullptr);
        REQUIRE(b.name == nullptr);
    }
    // OLED lane names flow through the registry lookup.
    REQUIRE(std::string(VoicePresets::getSequencerParamName(29, ParamId::Filter)) == "JAWARI");
    REQUIRE(std::string(VoicePresets::getSequencerParamName(29, ParamId::Attack)) == "PICK");
    REQUIRE(std::string(VoicePresets::getSequencerParamName(29, ParamId::Decay)) == "TARAF");
    REQUIRE(VoicePresets::getSequencerParamName(29, ParamId::Velocity) == nullptr);
}

TEST_CASE("Sitar preset seeding lands the resting values in the sitar controls",
          "[sitar_preset]")
{
    const auto &c = VoicePresets::getSitarVoice();
    Sequencer seq;
    seq.initializeParameters();
    const float noteBefore = seq.getStepParameterValue(ParamId::Note, 0);
    VoiceParameters::seedTracks(seq, c);
    const uint8_t filterSteps = seq.getParameterStepCount(ParamId::Filter);
    REQUIRE(filterSteps > 0);
    // Tracks hold normalized lane values; each resting value sits on its
    // lane midpoint, so every seeded step is exactly 0.5.
    const auto &jawari = VoiceParameters::binding(c, ParamId::Filter);
    const auto &pick = VoiceParameters::binding(c, ParamId::Attack);
    const auto &taraf = VoiceParameters::binding(c, ParamId::Decay);
    for (uint8_t step = 0; step < filterSteps; ++step)
    {
        REQUIRE(seq.getStepParameterValue(ParamId::Filter, step) ==
                jawari.normalize(c.sitarJawari));
        REQUIRE(seq.getStepParameterValue(ParamId::Attack, step) ==
                pick.normalize(c.sitarPickHardness));
        REQUIRE(seq.getStepParameterValue(ParamId::Decay, step) ==
                taraf.normalize(c.sitarTarafAmount));
        REQUIRE(seq.getStepParameterValue(ParamId::Filter, step) == 0.5f);
        REQUIRE(seq.getStepParameterValue(ParamId::Attack, step) == 0.5f);
        REQUIRE(seq.getStepParameterValue(ParamId::Decay, step) == 0.5f);
    }
    // Musical lanes are not seeded by engine bindings.
    REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == noteBefore);

    // And the seeded lane values reach the sitar controls through the voice's
    // control queue: JAWARI/PICK/TARAF land in the right sitar fields.
    Voice voice(0, c);
    voice.init(kSampleRate);
    voice.updateParameters(seededState(c));
    voice.process(); // one applied control update per sample
    REQUIRE(voice.getConfig().sitarJawari == c.sitarJawari);
    REQUIRE(voice.getConfig().sitarPickHardness == c.sitarPickHardness);
    REQUIRE(voice.getConfig().sitarTarafAmount == c.sitarTarafAmount);
}

TEST_CASE("Sitar preset speaks, rings past the gate and settles silent",
          "[sitar_preset]")
{
    // Full chain, preset values: the pluck speaks and the tail rings on.
    const auto &c = VoicePresets::getSitarVoice();
    Voice voice(0, c);
    voice.init(kSampleRate);
    voice.updateParameters(seededState(c));
    float peak = 0.0f;
    for (int i = 0; i < 9600; ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak > 0.01f); // note-on speaks
    REQUIRE(peak < 1.0f);  // string + taraf + body mixes below PCM clipping

    // hasEnvelope == false: gate-off must NOT silence the voice.
    voice.setGate(false);
    std::array<float, 24000> tail{};
    for (size_t i = 0; i < tail.size(); ++i)
    {
        const float s = voice.process();
        REQUIRE(std::isfinite(s));
        tail[i] = s;
    }
    REQUIRE(windowRms(tail.data(), tail.size()) > 0.0006);

    // The natural decay then reaches exact silence and stays there (decay
    // shortened to keep the render short; the preset's musical 4 s tail is
    // the same mechanism).
    auto fast = VoicePresets::getSitarVoice();
    fast.sitarDecay = 0.5f;
    fast.sitarTarafDecay = 0.5f;
    Voice fastVoice(0, fast);
    fastVoice.init(kSampleRate);
    fastVoice.updateParameters(seededState(fast));
    for (int i = 0; i < 4800; ++i) (void)fastVoice.process(); // pluck, gate high
    fastVoice.setGate(false);
    fastVoice.process();
    for (int i = 0; i < 288000; ++i) (void)fastVoice.process(); // 6 s of decay
    for (int i = 0; i < 4800; ++i) REQUIRE(fastVoice.process() == 0.0f);
}
