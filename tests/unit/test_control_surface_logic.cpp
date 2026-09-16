// Unit tests for the Alchemy tile control surface decision logic
// (src/ui/ControlSurfaceLogic.h/.cpp): ModeStabilizer, PadBank, ShiftLatch,
// FaderMap. These are pure C++ — no hardware, no Arduino stubs needed.

#include "ui/ControlSurfaceLogic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using namespace ControlSurface;

TEST_CASE("Parameter record buttons select their matching encoder base", "[control_surface]")
{
    struct Mapping {
        ParamId param;
        EncoderParameterMode mode;
    };
    constexpr Mapping mappings[] = {
        {ParamId::Note, EncoderParameterMode::Note},
        {ParamId::Velocity, EncoderParameterMode::Velocity},
        {ParamId::Filter, EncoderParameterMode::Filter},
        {ParamId::Attack, EncoderParameterMode::Attack},
        {ParamId::Decay, EncoderParameterMode::Decay},
        {ParamId::Octave, EncoderParameterMode::Octave},
    };

    for (const auto &mapping : mappings)
    {
        EncoderParameterMode mode = EncoderParameterMode::COUNT;
        CAPTURE(static_cast<int>(mapping.param));
        REQUIRE(encoderBaseModeForRecordParam(mapping.param, mode));
        CHECK(mode == mapping.mode);
    }

    for (const ParamId nonRecordParam : {ParamId::GateLength, ParamId::Gate,
                                         ParamId::Slide, ParamId::Count})
    {
        EncoderParameterMode mode = EncoderParameterMode::COUNT;
        CAPTURE(static_cast<int>(nonRecordParam));
        CHECK_FALSE(encoderBaseModeForRecordParam(nonRecordParam, mode));
        CHECK(mode == EncoderParameterMode::COUNT);
    }
}

TEST_CASE("Octave base offsets preserve and add to sequencer octaves", "[control_surface]")
{
    CHECK(combineOctaveOffsets(-12, 0.0f) == -12);
    CHECK(combineOctaveOffsets(0, 0.0f) == 0);
    CHECK(combineOctaveOffsets(12, 0.0f) == 12);

    CHECK(combineOctaveOffsets(12, 0.75f) == 21);
    CHECK(combineOctaveOffsets(12, 1.0f) == 24);
    CHECK(combineOctaveOffsets(-12, -1.0f) == -24);
    CHECK(combineOctaveOffsets(12, 2.0f) == 24);
}

// ---------------------------------------------------------------------------
// ModeStabilizer
// ---------------------------------------------------------------------------

TEST_CASE("ModeStabilizer boots into the requested mode without an edge", "[control_surface]")
{
    ModeStabilizer m;
    m.begin(Mode::Utility, 1000);
    CHECK(m.mode() == Mode::Utility);
    CHECK_FALSE(m.tookChange());

    // A stable reading that agrees with the boot mode produces no edge.
    CHECK(m.update(false, 1100) == Mode::Utility);
    CHECK_FALSE(m.tookChange());
}

TEST_CASE("ModeStabilizer ignores glitches shorter than the stability window", "[control_surface]")
{
    ModeStabilizer m;
    m.begin(Mode::Param, 0);

    // GP7 goes high for 19 ms — one ms short of the 20 ms requirement.
    CHECK(m.update(true, 5) == Mode::Param);
    CHECK(m.update(true, 24) == Mode::Param);
    CHECK_FALSE(m.tookChange());

    // Back to low before the window elapsed: still Param, no flip ever happened.
    CHECK(m.update(false, 30) == Mode::Param);
    CHECK_FALSE(m.tookChange());
}

TEST_CASE("ModeStabilizer flips after a stable level and raises the edge once", "[control_surface]")
{
    ModeStabilizer m;
    m.begin(Mode::Param, 0);

    CHECK(m.update(true, 10) == Mode::Param);   // candidate starts
    CHECK(m.update(true, 29) == Mode::Param);   // 19 ms in: not yet
    CHECK(m.update(true, 30) == Mode::Utility); // 20 ms: flip
    CHECK(m.tookChange());
    m.clearChange();
    CHECK_FALSE(m.tookChange());

    // Continued agreement: no further edges.
    CHECK(m.update(true, 1000) == Mode::Utility);
    CHECK_FALSE(m.tookChange());
}

TEST_CASE("ModeStabilizer restarts the stability window when the input bounces", "[control_surface]")
{
    ModeStabilizer m;
    m.begin(Mode::Param, 0);

    CHECK(m.update(true, 0) == Mode::Param);
    CHECK(m.update(false, 10) == Mode::Param); // bounce restarts the wait
    CHECK(m.update(true, 20) == Mode::Param);  // only 10 ms since restart
    CHECK(m.update(true, 39) == Mode::Param);
    CHECK(m.update(true, 40) == Mode::Utility); // 20 ms of continuous high
    CHECK(m.tookChange());
}

TEST_CASE("ModeStabilizer maps raw level to mode via kModeParamLevel", "[control_surface]")
{
    // Design polarity: LOW = Param, HIGH = Utility (kModeParamLevel == false).
    CHECK_FALSE(kModeParamLevel);

    ModeStabilizer m;
    m.begin(Mode::Param, 0);
    CHECK(m.update(false, 0) == Mode::Param);
    CHECK(m.update(true, kModeStabilityMs) == Mode::Param); // candidate starts
    CHECK(m.update(true, 2 * kModeStabilityMs) == Mode::Utility); // 20 ms held

    // ...and back the other way (again needing the full stability window).
    CHECK(m.update(false, 2 * kModeStabilityMs) == Mode::Utility); // candidate starts
    CHECK(m.update(false, 3 * kModeStabilityMs) == Mode::Param);   // 20 ms held
}

// ---------------------------------------------------------------------------
// PadBank
// ---------------------------------------------------------------------------

TEST_CASE("PadBank maps selected voices to their pair banks", "[control_surface]")
{
    CHECK(PadBank::pairFor(0).lowVoice == 0);
    CHECK(PadBank::pairFor(0).highVoice == 1);
    CHECK(PadBank::pairFor(1).lowVoice == 0);
    CHECK(PadBank::pairFor(1).highVoice == 1);
    CHECK(PadBank::pairFor(2).lowVoice == 2);
    CHECK(PadBank::pairFor(2).highVoice == 3);
    CHECK(PadBank::pairFor(3).lowVoice == 2);
    CHECK(PadBank::pairFor(3).highVoice == 3);
}

TEST_CASE("PadBank clamps out-of-range selected voices", "[control_surface]")
{
    CHECK(PadBank::pairFor(4).lowVoice == 0);
    CHECK(PadBank::pairFor(4).highVoice == 1);
    CHECK(PadBank::pairFor(200).highVoice == 1);
}

TEST_CASE("PadBank resolves the low bank to the pair's first voice", "[control_surface]")
{
    for (uint8_t step = 0; step < 16; ++step)
    {
        const PadAddress a = PadBank::resolve(step, 2); // voices 3+4 visible
        CHECK(a.voice == 2);
        CHECK(a.step == step);
    }
}

TEST_CASE("PadBank resolves the high bank to the pair's second voice", "[control_surface]")
{
    for (uint8_t step = 0; step < 16; ++step)
    {
        const PadAddress a = PadBank::resolve(static_cast<uint8_t>(16 + step), 2);
        CHECK(a.voice == 3);
        CHECK(a.step == step);
    }
}

TEST_CASE("PadBank resolution follows the selected voice across the pair boundary", "[control_surface]")
{
    // Voice 1 (index 0) selected: banks are voices 1+2 (indices 0+1).
    PadAddress a = PadBank::resolve(0, 0);
    CHECK(a.voice == 0);
    a = PadBank::resolve(31, 0);
    CHECK(a.voice == 1);
    CHECK(a.step == 15);

    // Voice 4 (index 3) selected: banks are voices 3+4 (indices 2+3).
    a = PadBank::resolve(0, 3);
    CHECK(a.voice == 2);
    a = PadBank::resolve(16, 3);
    CHECK(a.voice == 3);
}

TEST_CASE("PadBank clamps out-of-range pad indices", "[control_surface]")
{
    const PadAddress a = PadBank::resolve(32, 0);
    CHECK(a.voice == 1); // clamped to pad 31 -> high bank
    CHECK(a.step == 15);
}

// ---------------------------------------------------------------------------
// LedLayout (pad-mirror LED geometry)
// ---------------------------------------------------------------------------

TEST_CASE("LedLayout mirrors the touch-pad geometry", "[control_surface]")
{
    // The 8x4 LED panel mirrors the 4x8 touch matrix: band b covers LED
    // linear indices b*16..b*16+15, and touch pads b*16..b*16+15 occupy
    // rows 2b..2b+1. The same (band, step) must resolve to the same index
    // on both surfaces.
    for (uint8_t band = 0; band < LedLayout::kBandCount; ++band)
    {
        for (uint8_t step = 0; step < LedLayout::kStepsPerBand; ++step)
        {
            const uint8_t padRow = static_cast<uint8_t>(2 * band + step / LedLayout::kWidth);
            const uint8_t padCol = static_cast<uint8_t>(step % LedLayout::kWidth);
            const uint8_t padIndex = static_cast<uint8_t>(padRow * LedLayout::kWidth + padCol);
            CHECK(LedLayout::linearIndex(band, step) == static_cast<int>(padIndex));
            CHECK(LedLayout::x(step) == static_cast<int>(padCol));
            CHECK(LedLayout::y(band, step) == static_cast<int>(padRow));
        }
    }
    CHECK(LedLayout::kLedCount == 32);
    CHECK(LedLayout::kStepsPerBand == 16);
}

TEST_CASE("LedLayout rejects out-of-range coordinates", "[control_surface]")
{
    CHECK(LedLayout::linearIndex(LedLayout::kBandCount, 0) == -1);
    CHECK(LedLayout::linearIndex(0, LedLayout::kStepsPerBand) == -1);
    CHECK(LedLayout::y(LedLayout::kBandCount, 0) == -1);
    CHECK(LedLayout::y(0, LedLayout::kStepsPerBand) == -1);
    CHECK(LedLayout::x(LedLayout::kStepsPerBand) == -1);
}

TEST_CASE("LedLayout maps selected voices onto pair bands", "[control_surface]")
{
    CHECK(LedLayout::bandOfVoiceInPair(0) == 0);
    CHECK(LedLayout::bandOfVoiceInPair(1) == 1);
    CHECK(LedLayout::bandOfVoiceInPair(2) == 0);
    CHECK(LedLayout::bandOfVoiceInPair(3) == 1);
    CHECK(LedLayout::bandOfVoiceInPair(4) == 0); // clamped like PadBank::pairFor
}

// ---------------------------------------------------------------------------
// ShiftLatch
// ---------------------------------------------------------------------------

namespace
{
constexpr uint8_t kNote = static_cast<uint8_t>(ParamId::Note);
constexpr uint8_t kVelocity = static_cast<uint8_t>(ParamId::Velocity);
constexpr uint8_t kFilter = static_cast<uint8_t>(ParamId::Filter);
} // namespace

TEST_CASE("ShiftLatch ordinary press/release is a momentary hold", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};

    latch.onParamButton(kFilter, true, /*shiftHeld=*/false);
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(held[kFilter]);
    CHECK(latch.latched() == ShiftLatch::kNoLatch);

    latch.onParamButton(kFilter, false, false);
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK_FALSE(held[kFilter]);
}

TEST_CASE("ShiftLatch shift+tap latches a param that stays held on release", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};

    latch.onParamButton(kFilter, true, true);
    latch.onParamButton(kFilter, false, true); // finger off, latch persists
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(latch.latched() == static_cast<int8_t>(kFilter));
    CHECK(held[kFilter]);
}

TEST_CASE("ShiftLatch shift+tapping the latched param unlatches it", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};

    latch.onParamButton(kFilter, true, true);
    latch.onParamButton(kFilter, false, true);
    latch.onParamButton(kFilter, true, true);  // second shift+tap...
    latch.onParamButton(kFilter, false, true); // ...releases the finger
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(latch.latched() == ShiftLatch::kNoLatch);
    CHECK_FALSE(held[kFilter]);
}

TEST_CASE("ShiftLatch latching another param moves the single latch", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};

    latch.onParamButton(kFilter, true, true);
    latch.onParamButton(kFilter, false, true);
    latch.onParamButton(kVelocity, true, true); // moves the latch...
    latch.onParamButton(kVelocity, false, true);

    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(latch.latched() == static_cast<int8_t>(kVelocity));
    CHECK(held[kVelocity]);
    CHECK_FALSE(held[kFilter]); // ...and releases the previous one
}

TEST_CASE("ShiftLatch keeps ordinary momentary holds alongside the latch", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};

    latch.onParamButton(kFilter, true, true); // latched
    latch.onParamButton(kFilter, false, true);

    latch.onParamButton(kNote, true, false); // plain hold with no shift
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(held[kFilter]); // latched, no finger
    CHECK(held[kNote]);   // momentary alongside it

    latch.onParamButton(kNote, false, false);
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(held[kFilter]);
    CHECK_FALSE(held[kNote]);
}

TEST_CASE("ShiftLatch applyTo rewrites the whole array with no stale holds", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};
    held[kVelocity] = true; // stale bit from some other code path

    latch.onParamButton(kNote, true, true);
    latch.onParamButton(kNote, false, true);
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(held[kNote]);
    CHECK_FALSE(held[kVelocity]); // rewritten away
}

TEST_CASE("ShiftLatch reset clears everything", "[control_surface]")
{
    ShiftLatch latch;
    bool held[ShiftLatch::kParamCount] = {false};

    latch.onParamButton(kFilter, true, true);
    latch.onParamButton(kVelocity, true, false); // finger still down
    latch.reset();
    latch.applyTo(held, ShiftLatch::kParamCount);
    CHECK(latch.latched() == ShiftLatch::kNoLatch);
    CHECK_FALSE(latch.isMomentary(kVelocity));
    for (bool h : held)
    {
        CHECK_FALSE(h);
    }
}

TEST_CASE("ShiftLatch ignores out-of-range param ids", "[control_surface]")
{
    ShiftLatch latch;
    latch.onParamButton(255, true, true);
    CHECK(latch.latched() == ShiftLatch::kNoLatch);
}

// ---------------------------------------------------------------------------
// FaderMap
// ---------------------------------------------------------------------------

TEST_CASE("FaderMap assigns param-mode faders to Filter/Attack/Decay/Velocity", "[control_surface]")
{
    const FaderAssignment ch0 = FaderMap::assignmentFor(Mode::Param, 0);
    CHECK(ch0.target == FaderTarget::StepParam);
    CHECK(ch0.paramId == ParamId::Filter);

    const FaderAssignment ch1 = FaderMap::assignmentFor(Mode::Param, 1);
    CHECK(ch1.paramId == ParamId::Attack);

    const FaderAssignment ch2 = FaderMap::assignmentFor(Mode::Param, 2);
    CHECK(ch2.paramId == ParamId::Decay);

    const FaderAssignment ch3 = FaderMap::assignmentFor(Mode::Param, 3);
    CHECK(ch3.paramId == ParamId::Velocity);
}

TEST_CASE("FaderMap assigns utility-mode faders to tempo/swing/master-volume/gate-length", "[control_surface]")
{
    CHECK(FaderMap::assignmentFor(Mode::Utility, 0).target == FaderTarget::Tempo);
    CHECK(FaderMap::assignmentFor(Mode::Utility, 1).target == FaderTarget::SwingAmount);
    CHECK(FaderMap::assignmentFor(Mode::Utility, 2).target == FaderTarget::MasterVolume);
    CHECK(FaderMap::assignmentFor(Mode::Utility, 3).target == FaderTarget::GateLength);
}

TEST_CASE("FaderMap rejects out-of-range channels", "[control_surface]")
{
    const FaderAssignment bad = FaderMap::assignmentFor(Mode::Param, 4);
    CHECK(bad.paramId == ParamId::Count);
    CHECK_FALSE(FaderMap().accept(4, 100));
}

TEST_CASE("FaderMap deadband requires an obvious move to engage then tracks real movement", "[control_surface]")
{
    FaderMap map;

    // First sample establishes baseline, does NOT send
    CHECK_FALSE(map.accept(0, 2048));
    CHECK_FALSE(map.isEngaged(0));

    // Jitter and small movements below move threshold (64 counts) are rejected
    CHECK_FALSE(map.accept(0, 2050));        // +2 counts from baseline
    CHECK_FALSE(map.accept(0, 2100));        // +52 counts: still < 64
    CHECK_FALSE(map.isEngaged(0));

    // Moving >= 64 counts from baseline engages the fader and sends the current value
    CHECK(map.accept(0, 2112));              // +64 counts: engaged and sent!
    CHECK(map.isEngaged(0));

    // Once engaged, standard 8-count deadband applies:
    CHECK_FALSE(map.accept(0, 2115));        // +3 counts from last sent (2112): rejected
    CHECK_FALSE(map.accept(0, 2119));        // +7 counts from 2112: rejected
    CHECK(map.accept(0, 2120));              // +8 counts from 2112: sent!
}

TEST_CASE("FaderMap channels engage independently", "[control_surface]")
{
    FaderMap map;
    CHECK_FALSE(map.accept(0, 1000));
    CHECK_FALSE(map.accept(1, 2000));

    // Move channel 0 beyond threshold
    CHECK(map.accept(0, 1064));
    CHECK(map.isEngaged(0));
    CHECK_FALSE(map.isEngaged(1));

    // Channel 1 still unengaged and small change rejected
    CHECK_FALSE(map.accept(1, 2020));
    CHECK_FALSE(map.isEngaged(1));

    // Move channel 1 downward beyond threshold
    CHECK(map.accept(1, 1936)); // -64 counts from 2000
    CHECK(map.isEngaged(1));
}

TEST_CASE("FaderMap resetDeadband disarms channels until moved again", "[control_surface]")
{
    FaderMap map;
    CHECK_FALSE(map.accept(2, 3000)); // seed baseline
    CHECK(map.accept(2, 3070));       // +70: engaged!
    CHECK(map.isEngaged(2));

    map.resetDeadband();              // mode flip disarms all channels
    CHECK_FALSE(map.isEngaged(2));

    // First sample in new mode establishes new baseline without sending
    CHECK_FALSE(map.accept(2, 3070));
    CHECK_FALSE(map.isEngaged(2));

    // Small changes around 3070 do not send
    CHECK_FALSE(map.accept(2, 3080)); // +10 counts < 64
    CHECK_FALSE(map.isEngaged(2));

    // Obvious move in new mode engages channel 2
    CHECK(map.accept(2, 3134));       // +64 counts from 3070
    CHECK(map.isEngaged(2));
}

TEST_CASE("FaderMap disarms across voice switches", "[control_surface]")
{
    FaderMap map;

    // Voice 0: Move fader 3 (Gate Length in utility mode) and engage it
    CHECK_FALSE(map.accept(3, 1000));
    CHECK(map.accept(3, 1100)); // +100 counts >= 64: engaged
    CHECK(map.isEngaged(3));

    // Voice switch occurs: bridge calls resetDeadband()
    map.resetDeadband();
    CHECK_FALSE(map.isEngaged(3));

    // Voice 1: First sample after switch seeds baseline without sending
    CHECK_FALSE(map.accept(3, 1100));
    CHECK_FALSE(map.isEngaged(3));

    // Small jitter / touch on Voice 1 is ignored
    CHECK_FALSE(map.accept(3, 1110)); // +10 counts < 64
    CHECK_FALSE(map.isEngaged(3));

    // Intentional move on Voice 1 engages fader
    CHECK(map.accept(3, 1200)); // +100 counts >= 64
    CHECK(map.isEngaged(3));
}

TEST_CASE("FaderMap normalize maps 12-bit counts to 0..1", "[control_surface]")
{
    CHECK(FaderMap::normalize(0) == Catch::Approx(0.0f).margin(0.0001f));
    CHECK(FaderMap::normalize(4095) == Catch::Approx(1.0f).margin(0.0001f));
    CHECK(FaderMap::normalize(2048) == Catch::Approx(0.5f).margin(0.001f));
}

// ---------------------------------------------------------------------------
// EncoderMotion
// ---------------------------------------------------------------------------

namespace
{
constexpr float kNoiseFloor = 0.0005f;
constexpr float kDetent = 0.03f;
} // namespace

TEST_CASE("EncoderMotion keeps slow turns that are below the floor per read", "[control_surface][encoder]")
{
    // A slow TMAG5273 turn gives increments far below the floor on every
    // read. Discarding each read lost the whole turn; accumulating keeps it.
    EncoderMotion motion;
    const float slowRead = kNoiseFloor / 10.0f;
    for (int read = 0; read < 9; ++read)
    {
        motion.add(slowRead);
        CHECK(motion.takeContinuous(kNoiseFloor) == 0.0f);
    }
    motion.add(2.0f * slowRead);
    CHECK(motion.takeContinuous(kNoiseFloor) == Catch::Approx(1.1f * kNoiseFloor));
    CHECK(motion.pending() == 0.0f);
}

TEST_CASE("EncoderMotion passes large increments through at once", "[control_surface][encoder]")
{
    EncoderMotion motion;
    motion.add(-0.2f);
    CHECK(motion.takeContinuous(kNoiseFloor) == Catch::Approx(-0.2f));
    CHECK(motion.takeContinuous(kNoiseFloor) == 0.0f);
}

TEST_CASE("EncoderMotion discards pending motion on a reversal", "[control_surface][encoder]")
{
    EncoderMotion motion;
    motion.add(kNoiseFloor * 0.9f);
    motion.add(-kNoiseFloor * 0.1f);
    CHECK(motion.pending() == Catch::Approx(-kNoiseFloor * 0.1f));

    // Jitter alternates direction, so it never adds up to a change.
    for (int read = 0; read < 1000; ++read)
    {
        motion.add((read % 2 == 0 ? 1.0f : -1.0f) * kNoiseFloor * 0.6f);
        CHECK(motion.takeContinuous(kNoiseFloor) == 0.0f);
    }
}

TEST_CASE("EncoderMotion steps once per detent and keeps the remainder", "[control_surface][encoder]")
{
    EncoderMotion motion;
    motion.add(kDetent * 0.5f);
    CHECK(motion.takeSteps(kDetent) == 0);
    motion.add(kDetent * 0.6f);
    CHECK(motion.takeSteps(kDetent) == 1);
    CHECK(motion.pending() == Catch::Approx(kDetent * 0.1f));

    motion.add(kDetent * 2.5f); // a fast twist passes several detents in one read
    CHECK(motion.takeSteps(kDetent) == 2);

    motion.reset();
    motion.add(-kDetent * 3.2f);
    CHECK(motion.takeSteps(kDetent) == -3);
    CHECK(motion.pending() == Catch::Approx(-kDetent * 0.2f).margin(1e-6));
}

TEST_CASE("EncoderMotion ignores zero, non-finite and non-positive sizes", "[control_surface][encoder]")
{
    EncoderMotion motion;
    motion.add(0.01f);
    motion.add(0.0f);
    motion.add(std::nanf(""));
    motion.add(std::numeric_limits<float>::infinity());
    CHECK(motion.pending() == Catch::Approx(0.01f));
    CHECK(motion.takeSteps(0.0f) == 0);
    CHECK(motion.takeSteps(-kDetent) == 0);
    CHECK(motion.pending() == Catch::Approx(0.01f));
}
TEST_CASE("Step edit targets the held, then toggled, then encoder parameter", "[control_surface]")
{
    CHECK(stepEditParameter(ParamId::Filter, ParamId::Velocity, EncoderParameterMode::Decay) == ParamId::Filter);
    CHECK(stepEditParameter(ParamId::Count, ParamId::Velocity, EncoderParameterMode::Decay) == ParamId::Velocity);
    CHECK(stepEditParameter(ParamId::Count, ParamId::Count, EncoderParameterMode::Decay) == ParamId::Decay);
    CHECK(stepEditParameter(ParamId::Count, ParamId::Count, EncoderParameterMode::Note) == ParamId::Note);
    CHECK(stepEditParameter(ParamId::Count, ParamId::Count, EncoderParameterMode::Octave) == ParamId::Octave);
    // Slide Time is a voice setting, not a step lane.
    CHECK(stepEditParameter(ParamId::Count, ParamId::Count, EncoderParameterMode::SlideTime) == ParamId::Count);
}
