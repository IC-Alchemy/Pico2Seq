#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "utils/DspMapping.h"
#include "voice/VoiceParameters.h"

// Sweet-spot lane mapping: the true-exponential OCT curve and the centered
// piecewise map that puts a lane's midpoint on a chosen operating point.

using namespace Catch::Matchers;
using dspmap::Mapping;

TEST_CASE("fmap OCT is a true exponential between its endpoints", "[mapping]") {
    REQUIRE_THAT(dspmap::fmap(0.0f, 100.0f, 8000.0f, Mapping::OCT), WithinAbs(100.0f, 1e-3f));
    REQUIRE_THAT(dspmap::fmap(1.0f, 100.0f, 8000.0f, Mapping::OCT), WithinAbs(8000.0f, 1e-2f));
    // The midpoint is the geometric mean: sqrt(100 * 8000).
    REQUIRE_THAT(dspmap::fmap(0.5f, 100.0f, 8000.0f, Mapping::OCT),
                 WithinRel(std::sqrt(100.0f * 8000.0f), 1e-5f));
    // Equal travel covers equal octaves: 100..6400 Hz is six octaves.
    for (int octave = 0; octave <= 6; ++octave)
        REQUIRE_THAT(dspmap::fmap(octave / 6.0f, 100.0f, 6400.0f, Mapping::OCT),
                     WithinRel(100.0f * std::pow(2.0f, float(octave)), 1e-4f));
}

TEST_CASE("fmap OCT matches the existing LOG taper", "[mapping]") {
    for (float in = 0.0f; in <= 1.0001f; in += 0.125f)
        REQUIRE_THAT(dspmap::fmap(in, 0.2f, 4.0f, Mapping::OCT),
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
        REQUIRE_THAT(dspmap::fmapCentered(0.25f, 100.0f, 6400.0f, 400.0f, Mapping::OCT), WithinRel(200.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(0.5f, 100.0f, 6400.0f, 400.0f, Mapping::OCT), WithinRel(400.0f, 1e-5f));
        REQUIRE_THAT(dspmap::fmapCentered(0.75f, 100.0f, 6400.0f, 400.0f, Mapping::OCT), WithinRel(1600.0f, 1e-4f));
    }
    SECTION("square halves") {
        REQUIRE_THAT(dspmap::fmapCentered(0.25f, 0.0f, 1.0f, 0.3f, Mapping::EXP), WithinAbs(0.075f, 1e-5f));
        REQUIRE_THAT(dspmap::fmapCentered(0.75f, 0.0f, 1.0f, 0.3f, Mapping::EXP), WithinAbs(0.475f, 1e-5f));
    }
    SECTION("out-of-range input clamps") {
        REQUIRE_THAT(dspmap::fmapCentered(-1.0f, 100.0f, 900.0f, 300.0f, Mapping::LINEAR), WithinAbs(100.0f, 1e-4f));
        REQUIRE_THAT(dspmap::fmapCentered(2.0f, 100.0f, 900.0f, 300.0f, Mapping::OCT), WithinRel(900.0f, 1e-5f));
    }
}

TEST_CASE("fmapCentered is continuous and monotonic across the center", "[mapping]") {
    for (Mapping curve : {Mapping::LINEAR, Mapping::EXP, Mapping::OCT}) {
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
    for (Mapping curve : {Mapping::LINEAR, Mapping::EXP, Mapping::OCT}) {
        for (int i = 0; i <= 20; ++i) {
            const float n = i / 20.0f;
            const float value = dspmap::fmapCentered(n, 100.0f, 6400.0f, 400.0f, curve);
            REQUIRE_THAT(dspmap::normalizeCentered(value, 100.0f, 6400.0f, 400.0f, curve), WithinAbs(n, 1e-4f));
        }
    }
    REQUIRE(dspmap::normalizeCentered(50.0f, 100.0f, 6400.0f, 400.0f, Mapping::OCT) == 0.0f);
    REQUIRE_THAT(dspmap::normalizeCentered(9000.0f, 100.0f, 6400.0f, 400.0f, Mapping::OCT), WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("A center on the natural midpoint reproduces the plain curve", "[mapping]") {
    const float geometricMid = dspmap::fmap(0.5f, 100.0f, 1000.0f, Mapping::OCT);
    const float value = dspmap::fmap(0.3f, 100.0f, 1000.0f, Mapping::OCT);
    REQUIRE_THAT(dspmap::normalizeCentered(value, 100.0f, 1000.0f, geometricMid, Mapping::OCT), WithinAbs(0.3f, 1e-4f));
    for (float in = 0.0f; in <= 1.0001f; in += 0.1f)
        REQUIRE_THAT(dspmap::fmapCentered(in, 100.0f, 900.0f, 500.0f, Mapping::LINEAR),
                     WithinAbs(dspmap::fmap(in, 100.0f, 900.0f, Mapping::LINEAR), 1e-3f));
}

TEST_CASE("A center on a range end leaves one half flat but finite", "[mapping]") {
    // WgBell's pick hardness rests at its maximum.
    REQUIRE_THAT(dspmap::fmapCentered(0.9f, 0.7f, 1.0f, 1.0f, Mapping::LINEAR), WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(dspmap::normalizeCentered(1.0f, 0.7f, 1.0f, 1.0f, Mapping::LINEAR), WithinAbs(0.5f, 1e-6f));
    REQUIRE_THAT(dspmap::normalizeCentered(0.7f, 0.7f, 1.0f, 0.7f, Mapping::OCT), WithinAbs(0.5f, 1e-6f));
}

// ─── VoiceParameterBinding centers ───────────────────────────────────────────

TEST_CASE("A centered binding maps lane 0.5 to its center", "[mapping][voice]") {
    const VoiceParameterBinding b{"T60", nullptr, 0.2f, 4.0f, Mapping::OCT,
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
    const VoiceParameterBinding oct{"X", nullptr, 0.2f, 4.0f, Mapping::OCT,
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
