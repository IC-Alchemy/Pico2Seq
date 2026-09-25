#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// Arduino.h first, as firmware sources include it: its print-base macros (OCT,
// HEX, ...) must not collide with names in the headers below.
#include <Arduino.h>
#include "utils/DspMapping.h"
#include "voice/MusicalValues.h"
#include "voice/Voice.h"
#include "voice/VoiceEditParameters.h"
#include "voice/VoiceParameters.h"
#include "voice/VoicePresets.h"
#include "pico2seq-core/sequencer/SequencerDefs.h"
#include "sensors/SensorConstants.h"

// Sweet-spot lane mapping: the true-exponential OCTAVE curve and the centered
// piecewise map that puts a lane's midpoint on a chosen operating point.

using namespace Catch::Matchers;
using dspmap::Mapping;

TEST_CASE("fmap OCTAVE is a true exponential between its endpoints", "[mapping]") {
    REQUIRE_THAT(dspmap::fmap(0.0f, 100.0f, 8000.0f, Mapping::OCTAVE), WithinAbs(100.0f, 1e-3f));
    REQUIRE_THAT(dspmap::fmap(1.0f, 100.0f, 8000.0f, Mapping::OCTAVE), WithinAbs(8000.0f, 1e-2f));
    // The midpoint is the geometric mean: sqrt(100 * 8000).
    REQUIRE_THAT(dspmap::fmap(0.5f, 100.0f, 8000.0f, Mapping::OCTAVE),
                 WithinRel(std::sqrt(100.0f * 8000.0f), 1e-5f));
    // Equal travel covers equal octaves: 100..6400 Hz is six octaves.
    for (int octave = 0; octave <= 6; ++octave)
        REQUIRE_THAT(dspmap::fmap(octave / 6.0f, 100.0f, 6400.0f, Mapping::OCTAVE),
                     WithinRel(100.0f * std::pow(2.0f, float(octave)), 1e-4f));
}

TEST_CASE("fmap OCTAVE matches the existing LOG taper", "[mapping]") {
    for (float in = 0.0f; in <= 1.0001f; in += 0.125f)
        REQUIRE_THAT(dspmap::fmap(in, 0.2f, 4.0f, Mapping::OCTAVE),
                     WithinRel(dspmap::fmap(in, 0.2f, 4.0f, Mapping::LOG), 1e-5f));
}

TEST_CASE("fmapCentered puts the lane midpoint on the center", "[mapping]") {
    SECTION("linear halves") {
        // Center nearer min: 0..0.5 covers 100..300, 0.5..1 covers 300..900.
        REQUIRE_THAT(dspmap::fmapCentered(0.5f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(300.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(0.25f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(200.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(0.75f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(600.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(0.0f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(100.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(1.0f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(900.0f, 1e-4f));
    }
    SECTION("octave halves") {
        // Each half is exponential: lane 0.25 is the geometric mean of min and center.
        REQUIRE_THAT(dspmap::fmapCentered(0.25f, 100.0f, 6400.0f, 400.0f, Mapping::OCTAVE), WithinRel(200.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(0.5f, 100.0f, 6400.0f, 400.0f, Mapping::OCTAVE), WithinRel(400.0f, 1e-5f));
        REQUIRE_THAT(dspmap::fmapCentered(0.75f, 100.0f, 6400.0f, 400.0f, Mapping::OCTAVE), WithinRel(1600.0f, 1e-4f));
    }
    SECTION("square halves") {
        REQUIRE_THAT(dspmap::fmapCentered(0.25f, 0.0f, 1.0f, 0.3f, Mapping::EXP), WithinAbs(0.075f, 1e-5f));
        REQUIRE_THAT(dspmap::fmapCentered(0.75f, 0.0f, 1.0f, 0.3f, Mapping::EXP), WithinAbs(0.475f, 1e-5f));
    }
    SECTION("out-of-range input clamps") {
        REQUIRE_THAT(dspmap::fmapCentered(-1.0f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(100.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(2.0f, 100.0f, 900.0f, 300.0f, Mapping::OCTAVE), WithinRel(900.0f, 1e-5f));
    }
}

TEST_CASE("fmapCentered is continuous and monotonic across the center", "[mapping]") {
    for (Mapping curve : {Mapping::LINEAR, Mapping::EXP, Mapping::OCTAVE}) {
        float previous = dspmap::fmapCentered(0.0f, 60.0f, 1500.0f, 320.0f, curve);
        for (int i = 1; i <= 1000; ++i) {
            const float value = dspmap::fmapCentered(i / 1000.0f, 60.0f, 1500.0f, 320.0f, curve);
            REQUIRE(value >= previous);
            previous = value;
        }
        REQUIRE_THAT(dspmap::fmapCentered(0.4999f, 60.0f, 1500.0f, 320.0f, curve), WithinRel(320.0f, 1e-2f));
    }
}

TEST_CASE("normalizeCentered inverts fmapCentered", "[mapping]") {
    for (Mapping curve : {Mapping::LINEAR, Mapping::EXP, Mapping::OCTAVE}) {
        for (int i = 0; i <= 20; ++i) {
            const float n = i / 20.0f;
            const float value = dspmap::fmapCentered(n, 100.0f, 6400.0f, 400.0f, curve);
            REQUIRE_THAT(dspmap::normalizeCentered(value, 100.0f, 6400.0f, 400.0f, curve), WithinAbs(n, 1e-4f));
        }
    }
    REQUIRE(dspmap::normalizeCentered(50.0f, 100.0f, 6400.0f, 400.0f, Mapping::OCTAVE) == 0.0f);
    REQUIRE_THAT(dspmap::normalizeCentered(9000.0f, 100.0f, 6400.0f, 400.0f, Mapping::OCTAVE), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("A center on the natural midpoint reproduces the plain curve", "[mapping]") {
    const float geometricMid = dspmap::fmap(0.5f, 100.0f, 1000.0f, Mapping::OCTAVE);
    const float value = dspmap::fmap(0.3f, 100.0f, 1000.0f, Mapping::OCTAVE);
    REQUIRE_THAT(dspmap::normalizeCentered(value, 100.0f, 1000.0f, geometricMid, Mapping::OCTAVE), WithinAbs(0.3f, 1e-4f));
    for (float in = 0.0f; in <= 1.0001f; in += 0.1f)
        REQUIRE_THAT(dspmap::fmapCentered(in, 100.0f, 900.0f, 500.0f, Mapping::LINEAR),
                     WithinAbs(dspmap::fmap(in, 100.0f, 900.0f, Mapping::LINEAR), 1e-3f));
}

TEST_CASE("A center on a range end leaves one half flat but finite", "[mapping]") {
    // WgBell's pick hardness rests at its maximum.
    REQUIRE_THAT(dspmap::fmapCentered(0.9f, 0.7f, 1.0f, 1.0f, Mapping::LINEAR), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(dspmap::normalizeCentered(1.0f, 0.7f, 1.0f, 1.0f, Mapping::LINEAR), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(dspmap::normalizeCentered(0.7f, 0.7f, 1.0f, 0.7f, Mapping::OCTAVE), WithinAbs(0.5f, 1e-6f));
}

// ─── VoiceParameterBinding centers ───────────────────────────────────────────

TEST_CASE("A centered binding maps lane 0.5 to its center", "[mapping][voice]") {
    const VoiceParameterBinding b{"T60", nullptr, 0.2f, 4.0f, Mapping::OCTAVE,
                                  VoiceParameterUnit::Seconds, true, 0.5f, 1.8f};
    REQUIRE(b.isCentered());
    REQUIRE_THAT(b.map(0.5f), WithinRel(1.8f, 1e-5f));
    REQUIRE_THAT(b.map(0.0f), WithinRel(0.2f, 1e-5f));
    REQUIRE_THAT(b.map(1.0f), WithinRel(4.0f, 1e-5f));
    REQUIRE_THAT(b.normalize(1.8f), WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(b.normalize(0.2f), WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(b.normalize(4.0f), WithinAbs(1.0f, 1e-5f));
    for (int i = 0; i <= 10; ++i)
        REQUIRE_THAT(b.normalize(b.map(i / 10.0f)), WithinAbs(i / 10.0f, 1e-4f));
}

TEST_CASE("Bindings without a center keep their existing curves", "[mapping][voice]") {
    const VoiceParameterBinding oct{"X", nullptr, 0.2f, 4.0f, Mapping::OCTAVE,
                                    VoiceParameterUnit::Seconds, true};
    REQUIRE_FALSE(oct.isCentered());
    REQUIRE_THAT(oct.map(0.5f), WithinRel(0.2f * std::sqrt(4.0f / 0.2f), 1e-5f));
    REQUIRE_THAT(oct.normalize(oct.map(0.3f)), WithinAbs(0.3f, 1e-4f));

    const VoiceParameterBinding square{"Y", nullptr, 0.05f, 10.0f, Mapping::EXP,
                                       VoiceParameterUnit::Seconds, true};
    REQUIRE_FALSE(square.isCentered());
    REQUIRE_THAT(square.map(0.5f), WithinAbs(dspmap::fmap(0.5f, 0.05f, 10.0f, Mapping::EXP), 1e-6f));
    REQUIRE_THAT(square.normalize(2.5f), WithinAbs(std::sqrt((2.5f - 0.05f) / 9.95f), 1e-5f));
}

TEST_CASE("The no-center marker survives fast-math builds", "[mapping][voice]") {
    // The firmware compiles with -ffast-math, which lets the compiler assume
    // NaN never occurs; a NaN marker could read as "centered" everywhere.
    REQUIRE(std::isfinite(VoiceParameterBinding::kUncentered));
    REQUIRE(std::isfinite(VoiceParameterBinding{}.center));
    REQUIRE_FALSE(VoiceParameterBinding{}.isCentered());
}

// ─── Envelope lane curves ────────────────────────────────────────────────────

TEST_CASE("Attack lane spans 1 ms to 2 s; decay keeps 1 ms to 10 s", "[mapping][voice]") {
    REQUIRE_THAT(MusicalValues::attackSeconds(0.0f), WithinRel(0.001f, 1e-4f));
    REQUIRE_THAT(MusicalValues::attackSeconds(0.5f), WithinRel(std::sqrt(0.001f * 2.0f), 1e-4f)); // ~45 ms
    REQUIRE_THAT(MusicalValues::attackSeconds(1.0f), WithinRel(2.0f, 1e-4f));
    REQUIRE_THAT(MusicalValues::attackSeconds(-1.0f), WithinRel(0.001f, 1e-4f));
    REQUIRE_THAT(MusicalValues::envelopeSeconds(0.5f), WithinRel(0.1f, 1e-4f));
    REQUIRE_THAT(MusicalValues::envelopeSeconds(1.0f), WithinRel(10.0f, 1e-4f));
    for (float seconds : {0.001f, 0.002f, 0.015f, 0.4f, 2.0f})
        REQUIRE_THAT(MusicalValues::attackSeconds(VoiceEdit::attackNormalize(seconds)), WithinRel(seconds, 1e-4f));
    REQUIRE_THAT(VoiceEdit::attackNormalize(5.0f), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(VoiceEdit::attackNormalize(0.0f), WithinAbs(0.0f, 1e-6f));
}

// ─── Preset lane layouts ─────────────────────────────────────────────────────

namespace {
struct CutoffSpot { const char *preset; float minimum, center, maximum; };
// Hz: full lane travel is the musical span, lane 0.5 the preset's resting cutoff.
constexpr CutoffSpot kOscillatorCutoffs[] = {
    {"Analog", 150.0f, 1800.0f, 6000.0f},    {"Digital", 200.0f, 1500.0f, 5000.0f},
    {"Bass", 60.0f, 320.0f, 1500.0f},        {"Lead", 200.0f, 1600.0f, 8000.0f},
    {"Square", 250.0f, 900.0f, 4000.0f},     {"Pad", 250.0f, 2200.0f, 10000.0f},
    {"Percussion", 800.0f, 4500.0f, 12000.0f}, {"SubFunk", 60.0f, 420.0f, 1600.0f},
    {"RubberSub", 90.0f, 320.0f, 1200.0f}};
} // namespace

TEST_CASE("Oscillator presets retain shared sequencer lanes", "[mapping][presets]") {
    for (const auto &spot : kOscillatorCutoffs) {
        INFO(spot.preset);
        const auto config = VoicePresets::getPresetConfigByName(spot.preset);
        for (ParamId id : {ParamId::Filter, ParamId::Attack, ParamId::Decay,
                           ParamId::Octave, ParamId::GateLength, ParamId::Gate,
                           ParamId::Slide, ParamId::Sustain, ParamId::Release}) {
            INFO(static_cast<int>(id));
            const auto &binding = VoiceParameters::binding(config, id);
            REQUIRE(binding.target == nullptr);
            REQUIRE_THAT(binding.map(0.37f), WithinAbs(0.37f, 1e-6f));
        }
    }
}

TEST_CASE("Oscillator presets own octave cutoff lanes centered on their resting cutoff", "[mapping][presets]") {
    for (const auto &spot : kOscillatorCutoffs) {
        INFO(spot.preset);
        auto c = VoicePresets::getPresetConfigByName(spot.preset);
        REQUIRE(c.parameters != nullptr);
        const auto &layout = VoiceParameters::layout(c);
        REQUIRE(layout.cutoffMinimum == spot.minimum);
        REQUIRE(layout.cutoffMaximum == spot.maximum);
        REQUIRE_THAT(VoiceParameters::mapCutoff(layout, 0.0f), WithinAbs(spot.minimum, 1.0f));
        REQUIRE_THAT(VoiceParameters::mapCutoff(layout, 0.5f), WithinAbs(spot.center, 1.0f));
        REQUIRE_THAT(VoiceParameters::mapCutoff(layout, 1.0f), WithinAbs(spot.maximum, 1.0f));
        float previous = 0.0f;
        for (int i = 0; i <= 100; ++i) {
            const float hz = VoiceParameters::mapCutoff(layout, i / 100.0f);
            REQUIRE(hz > previous);
            previous = hz;
        }

        // A neutral modifier rests on the center, and every readout agrees.
        VoiceEdit::enablePatch(c);
        const float rest = VoiceEdit::composeLane(ParamId::Filter, 0.5f, &c);
        REQUIRE_THAT(VoiceParameters::mapCutoff(layout, rest), WithinRel(spot.center, 1e-4f));
        char text[24];
        MusicalValues::format(ParamId::Filter, MusicalValues::baseStep(c), c, nullptr, 120.0f, text, sizeof(text));
        REQUIRE(std::string(text) == std::to_string(static_cast<int>(spot.center)) + "Hz");
        VoiceEdit::format(VoiceEdit::Id::Cutoff, c, text, sizeof(text));
        REQUIRE(std::string(text) == std::to_string(static_cast<int>(spot.center)) + " Hz");

        Voice voice(0, c);
        voice.init(48000.0f);
        VoiceState state;
        state.filterCutoff = rest;
        state.isGateHigh = true;
        voice.updateParameters(state);
        voice.process();
        REQUIRE_THAT(voice.getFilterFrequency(), WithinRel(spot.center, 1e-4f));
    }
}

TEST_CASE("Uncentered layouts keep the legacy square cutoff curve", "[mapping][presets]") {
    VoiceConfig standard{}; // engine-switched voices fall back to paramSet layouts
    const auto &layout = VoiceParameters::layout(standard);
    REQUIRE_FALSE(layout.cutoffCentered());
    for (float n : {0.0f, 0.37f, 0.5f, 1.0f})
        REQUIRE_THAT(VoiceParameters::mapCutoff(layout, n),
                     WithinAbs(dspmap::fmap(n, 120.0f, 5000.0f, Mapping::EXP), 1e-3f));
}

TEST_CASE("Hard sync follows the oscillator bank and keeps the preset cutoff lane", "[mapping][presets]") {
    const auto &analog = VoicePresets::getAnalogVoice();
    REQUIRE(analog.paramSet == PARAMSET_HARDSYNC);
    REQUIRE(std::string(VoiceParameters::binding(analog, ParamId::Note).name) == "Master");
    REQUIRE(std::string(VoiceParameters::binding(analog, ParamId::Velocity).name) == "Slave");
    REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(analog));

    auto c = VoicePresets::getDigitalVoice();
    VoiceEdit::enablePatch(c);
    const auto *owned = c.parameters;
    REQUIRE(owned != nullptr);
    REQUIRE(VoiceParameters::velocityToAmplitude(c));
    VoiceEdit::setValue(VoiceEdit::Id::Wave1, c, WAVE_HARDSYNC_SAW);
    REQUIRE(c.paramSet == PARAMSET_HARDSYNC);
    REQUIRE(c.parameters == owned);
    REQUIRE(std::string(VoiceParameters::binding(c, ParamId::Velocity).name) == "Slave");
    REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(c));
    REQUIRE_THAT(VoiceParameters::mapCutoff(VoiceParameters::layout(c), c.filterCutoffBase),
                 WithinRel(1500.0f, 1e-4f));
    VoiceEdit::setValue(VoiceEdit::Id::Wave1, c, WAVE_BSP_SQUARE);
    REQUIRE(c.paramSet == PARAMSET_STANDARD);
    REQUIRE(VoiceParameters::binding(c, ParamId::Velocity).name == nullptr);
    REQUIRE(VoiceParameters::velocityToAmplitude(c));
}

namespace {
struct LaneSpot { const char *name; float minimum, center, maximum; Mapping curve; };
void requireLane(const VoiceConfig &c, ParamId id, const LaneSpot &spot) {
    INFO("lane " << spot.name);
    const auto &b = VoiceParameters::binding(c, id);
    REQUIRE(b.name != nullptr);
    REQUIRE(std::string(b.name) == spot.name);
    REQUIRE(b.target != nullptr);
    REQUIRE(b.curve == spot.curve);
    REQUIRE(b.isCentered());
    REQUIRE(b.minimum == spot.minimum);
    REQUIRE(b.center == spot.center);
    REQUIRE(b.maximum == spot.maximum);
    REQUIRE_THAT(b.map(0.0f), WithinRel(spot.minimum, 1e-4f) || WithinAbs(spot.minimum, 1e-6f));
    REQUIRE_THAT(b.map(0.5f), WithinRel(spot.center, 1e-4f) || WithinAbs(spot.center, 1e-6f));
    REQUIRE_THAT(b.map(1.0f), WithinRel(spot.maximum, 1e-4f));
}
} // namespace

TEST_CASE("String presets own T60, brightness and pick lanes centered on their resting values", "[mapping][presets]") {
    struct StringSpots { const char *preset; LaneSpot t60, bright, pick; };
    const StringSpots strings[] = {
        {"WgPluck", {"T60", 0.15f, 1.8f, 4.0f, Mapping::OCTAVE}, {"Bright", 0.2f, 0.78f, 0.95f, Mapping::LINEAR},
         {"Pick", 0.2f, 0.85f, 1.0f, Mapping::LINEAR}},
        {"WgNylon", {"T60", 0.6f, 3.2f, 8.0f, Mapping::OCTAVE}, {"Bright", 0.05f, 0.28f, 0.6f, Mapping::LINEAR},
         {"Pick", 0.05f, 0.22f, 0.6f, Mapping::LINEAR}},
        {"WgBell", {"T60", 0.25f, 1.4f, 3.0f, Mapping::OCTAVE}, {"Bright", 0.55f, 0.9f, 0.98f, Mapping::LINEAR},
         {"Pick", 0.7f, 1.0f, 1.0f, Mapping::LINEAR}},
        {"WgShimmer", {"T60", 1.5f, 6.5f, 10.0f, Mapping::OCTAVE}, {"Bright", 0.3f, 0.55f, 0.8f, Mapping::LINEAR},
         {"Pick", 0.3f, 0.6f, 0.9f, Mapping::LINEAR}},
    };
    for (const auto &s : strings) {
        INFO(s.preset);
        const auto &c = VoicePresets::getPresetConfigByName(s.preset);
        REQUIRE(c.parameters != nullptr);
        REQUIRE_FALSE(VoiceParameters::layout(c).envelopeFromTracks);
        REQUIRE_FALSE(VoiceParameters::velocityToAmplitude(c));
        requireLane(c, ParamId::Decay, s.t60);
        requireLane(c, ParamId::Filter, s.bright);
        requireLane(c, ParamId::Attack, s.pick);
        // The preset's resting string sits mid-lane.
        const auto &t60 = VoiceParameters::binding(c, ParamId::Decay);
        REQUIRE(t60.normalize(c.wgT60) >= 0.4f);
        REQUIRE(t60.normalize(c.wgT60) <= 0.6f);
    }
    // Engine-switched voices still fall back to the shared 0.05..10 s table.
    VoiceConfig fallback{};
    fallback.engine = ENGINE_WAVEGUIDE;
    fallback.paramSet = PARAMSET_WAVEGUIDE;
    const auto &t60 = VoiceParameters::binding(fallback, ParamId::Decay);
    REQUIRE(t60.minimum == VoiceParameters::kWaveguideT60Min);
    REQUIRE(t60.maximum == VoiceParameters::kWaveguideT60Max);
    REQUIRE_FALSE(t60.isCentered());
}

TEST_CASE("Texture presets own spans that keep their effect zones at the top", "[mapping][presets]") {
    const auto &hyper = VoicePresets::getHypersawVoice();
    REQUIRE(hyper.parameters != nullptr);
    requireLane(hyper, ParamId::Attack, {"Detune", 0.0f, 0.30f, 0.75f, Mapping::LINEAR});
    requireLane(hyper, ParamId::Decay, {"Mix", 0.15f, 0.50f, 0.95f, Mapping::LINEAR});
    REQUIRE(VoiceParameters::binding(hyper, ParamId::Filter).target == nullptr); // stays Cutoff
    const auto &cutoff = VoiceParameters::layout(hyper);
    REQUIRE(cutoff.cutoffMinimum == 200.0f);
    REQUIRE(cutoff.cutoffMaximum == 12000.0f);
    REQUIRE(hyper.filterCutoffBase == 0.5f);
    REQUIRE_THAT(VoiceParameters::mapCutoff(cutoff, hyper.filterCutoffBase), WithinRel(3200.0f, 1e-4f));

    const auto &storm = VoicePresets::getNoiseStormVoice();
    REQUIRE(storm.parameters != nullptr);
    requireLane(storm, ParamId::Filter, {"Color", 0.0f, 0.60f, 1.0f, Mapping::LINEAR});
    requireLane(storm, ParamId::Attack, {"Regen", 0.0f, 0.90f, 1.2f, Mapping::LINEAR});
    requireLane(storm, ParamId::Decay, {"Chaos", 0.0f, 0.40f, 0.8f, Mapping::LINEAR});
    // The documented 1.0..1.2 regen bloom is reachable from the lane's top.
    REQUIRE_THAT(VoiceParameters::binding(storm, ParamId::Attack).map(1.0f), WithinAbs(1.2f, 1e-6f));
    // The re-purposed Filter lane leaves the static cutoff on the shared curve.
    REQUIRE_FALSE(VoiceParameters::layout(storm).cutoffCentered());
}

TEST_CASE("Recipe presets own macro lanes centered on their musical operating points", "[mapping][presets][recipes]") {
    constexpr auto LIN = Mapping::LINEAR;
    struct Family { LaneSpot color, shape, character; };
    const Family fm{{"Index", 0.0f, 0.30f, 1.0f, Mapping::EXP}, {"Ratio", 0.5f, 2.0f, 4.77f, Mapping::OCTAVE},
                    {"Feedback", 0.0f, 0.10f, 0.35f, Mapping::EXP}};
    const Family phase{{"Shape", 0.0f, 0.5f, 1.0f, LIN}, {"Skew", -1.0f, 0.0f, 1.0f, LIN},
                       {"Blend", 0.0f, 0.5f, 1.0f, LIN}};
    const Family dsf{{"Bright", 0.0f, 0.45f, 0.9f, LIN}, {"Spacing", 0.5f, 2.0f, 5.07f, Mapping::OCTAVE},
                     {"Sub", 0.0f, 0.3f, 1.0f, LIN}};
    const Family prism{{"Focus", 0.0f, 0.45f, 1.0f, LIN}, {"Spread", 0.0f, 0.55f, 1.0f, LIN},
                       {"Drift", 0.0f, 0.30f, 0.85f, LIN}};
    const Family reed{{"Formant", 1.0f, 3.0f, 6.0f, Mapping::OCTAVE}, {"Bloom", 0.0f, 0.7f, 1.0f, LIN},
                      {"Body", 0.1f, 0.4f, 0.8f, LIN}};
    const Family silk{{"Silk", 0.0f, 0.25f, 0.65f, LIN}, {"Detune", 0.0f, 0.35f, 1.0f, LIN},
                      {"Blend", 0.1f, 0.35f, 0.6f, LIN}};
    const Family bell{{"Ratio", 0.5f, 2.0f, 6.0f, Mapping::OCTAVE}, {"Edge", 0.0f, 0.15f, 0.45f, LIN},
                      {"Ring", 0.0f, 0.5f, 0.85f, LIN}};
    const Family sync{{"Sync", 1.0f, 2.0f, 5.0f, Mapping::OCTAVE}, {"Edge", 0.0f, 0.25f, 0.65f, LIN},
                      {"Bite", 0.1f, 0.45f, 0.8f, LIN}};
    const Family orbit{{"Index", 0.0f, 1.2f, 3.0f, Mapping::EXP}, {"Ratio", 0.5f, 2.0f, 4.0f, LIN},
                       {"Body", 0.1f, 0.4f, 0.8f, LIN}};
    const Family chime{{"Focus", 0.0f, 0.3f, 0.8f, LIN}, {"Spread", 0.1f, 0.55f, 1.0f, LIN},
                       {"OctMix", 0.0f, 0.25f, 0.65f, LIN}};
    struct RecipePreset { const char *name; const Family *family; };
    const RecipePreset presets[] = {
        {"FMGlass", &fm},    {"FMBass", &fm},        {"VelvetKeys", &fm}, {"PhaseMorph", &phase},
        {"Spectral", &dsf},  {"CopperBass", &dsf},   {"Prism", &prism},   {"ChaosPrism", &prism},
        {"ReedPipe", &reed}, {"SilkPad", &silk},     {"HollowBell", &bell}, {"SyncLead", &sync},
        {"OrbitPluck", &orbit}, {"AirChime", &chime}};
    int recipes = 0;
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p)
        recipes += VoicePresets::getPresetConfig(p).engine == ENGINE_RECIPE;
    REQUIRE(recipes == static_cast<int>(std::size(presets)));
    for (const auto &preset : presets) {
        INFO(preset.name);
        const auto &c = VoicePresets::getPresetConfigByName(preset.name);
        REQUIRE(c.engine == ENGINE_RECIPE);
        const std::pair<ParamId, LaneSpot> lanes[] = {{ParamId::Filter, preset.family->color},
                                                      {ParamId::Attack, preset.family->shape},
                                                      {ParamId::Decay, preset.family->character}};
        for (const auto &[id, spot] : lanes) {
            requireLane(c, id, spot);
            // No preset base is glued to a rail.
            const auto &b = VoiceParameters::binding(c, id);
            const float rest = b.normalize(c.*(b.target));
            CHECK(rest >= 0.15f);
            CHECK(rest <= 0.85f);
        }
    }
}

TEST_CASE("Every preset owns a distinct lane layout", "[mapping][presets]") {
    std::vector<const VoiceParameterLayout *> seen;
    for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
        INFO(VoicePresets::getPresetName(p));
        const auto *layout = VoicePresets::getPresetConfig(p).parameters;
        REQUIRE(layout != nullptr);
        REQUIRE(std::find(seen.begin(), seen.end(), layout) == seen.end());
        seen.push_back(layout);
    }
}

TEST_CASE("Recipe selection keeps a preset's own lanes until the recipe changes", "[mapping][presets][recipes]") {
    using VoiceEdit::Id;
    auto c = VoicePresets::getPresetConfigByName("FMBass");
    const auto *own = c.parameters;
    const float fm = VoiceEdit::value(Id::Recipe, c);
    VoiceEdit::setValue(Id::Recipe, c, fm);
    REQUIRE(c.parameters == own);

    VoiceEdit::setValue(Id::Recipe, c, fm + 1.0f);
    REQUIRE(c.recipe == VoicePresets::getPresetConfigByName("PhaseMorph").recipe);
    REQUIRE(c.parameters == VoicePresets::getPresetConfigByName("PhaseMorph").parameters);
    for (ParamId lane : {ParamId::Filter, ParamId::Attack, ParamId::Decay}) {
        const auto &b = VoiceParameters::binding(c, lane);
        REQUIRE(c.*(b.target) >= b.minimum);
        REQUIRE(c.*(b.target) <= b.maximum);
    }
    VoiceEdit::setValue(Id::Recipe, c, fm);
    REQUIRE(c.parameters == VoicePresets::getPresetConfigByName("FMGlass").parameters);

    // A patch parked beyond a narrowed lane clamps onto its end.
    auto glass = VoicePresets::getPresetConfigByName("FMGlass");
    glass.macro2 = 6.0f;
    VoiceEdit::setValue(Id::Recipe, glass, VoiceEdit::value(Id::Recipe, glass));
    REQUIRE_THAT(glass.macro2, WithinAbs(4.77f, 1e-5f));
}

TEST_CASE("Octave parameter track distance zones map to discrete octaves", "[mapping][octave]") {
    // Distance thresholds:
    // -2 octaves: min (55 mm) to 90 mm
    // -1 octave:  91 mm to 280 mm
    //  0 octaves: 281 mm to 425 mm
    // +1 octave:  426 mm to 550 mm
    // +2 octaves: 551 mm to max (700 mm)

    auto normFromDistanceMm = [](int distanceMm) -> float {
        constexpr float minMm = SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
        constexpr float maxMm = SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
        const float clamped = std::clamp(static_cast<float>(distanceMm), minMm, maxMm);
        return (clamped - minMm) / (maxMm - minMm);
    };

    // Minimum boundary (55 mm) and upper edge of -2 zone (90 mm)
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(55)) == 0.0f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(55))) == -24);
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(90)) == 0.0f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(90))) == -24);

    // Lower edge of -1 zone (91 mm) and upper edge (280 mm)
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(91)) == 0.25f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(91))) == -12);
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(280)) == 0.25f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(280))) == -12);

    // Lower edge of 0 zone (281 mm) and upper edge (425 mm)
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(281)) == 0.5f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(281))) == 0);
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(425)) == 0.5f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(425))) == 0);

    // Midpoint normalization 0.5f sits safely in 0 octave zone
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, 0.5f) == 0.5f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, 0.5f)) == 0);

    // Lower edge of +1 zone (426 mm) and upper edge (550 mm)
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(426)) == 0.75f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(426))) == 12);
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(550)) == 0.75f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(550))) == 12);

    // Lower edge of +2 zone (551 mm) and max sensor height (700 mm)
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(551)) == 1.0f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(551))) == 24);
    REQUIRE(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(700)) == 1.0f);
    REQUIRE(VoiceEdit::mapOctave(mapNormalizedValueToParamRange(ParamId::Octave, normFromDistanceMm(700))) == 24);
}
