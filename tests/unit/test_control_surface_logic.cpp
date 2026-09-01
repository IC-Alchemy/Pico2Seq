// Unit tests for the Alchemy tile control surface decision logic
// (src/ui/ControlSurfaceLogic.h/.cpp): ModeStabilizer, PadBank, ShiftLatch,
// FaderMap. These are pure C++ — no hardware, no Arduino stubs needed.

#include "ui/ControlSurfaceLogic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace ControlSurface;

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

TEST_CASE("FaderMap assigns utility-mode faders to tempo/swing/delay/gate-length", "[control_surface]")
{
    CHECK(FaderMap::assignmentFor(Mode::Utility, 0).target == FaderTarget::Tempo);
    CHECK(FaderMap::assignmentFor(Mode::Utility, 1).target == FaderTarget::SwingAmount);
    CHECK(FaderMap::assignmentFor(Mode::Utility, 2).target == FaderTarget::DelayMix);
    CHECK(FaderMap::assignmentFor(Mode::Utility, 3).target == FaderTarget::GateLength);
}

TEST_CASE("FaderMap rejects out-of-range channels", "[control_surface]")
{
    const FaderAssignment bad = FaderMap::assignmentFor(Mode::Param, 4);
    CHECK(bad.paramId == ParamId::Count);
    CHECK_FALSE(FaderMap().accept(4, 100));
}

TEST_CASE("FaderMap deadband sends the first sample then only real movement", "[control_surface]")
{
    FaderMap map;

    CHECK(map.accept(0, 2048));              // first sample always sends
    CHECK_FALSE(map.accept(0, 2050));        // +2 counts: inside the deadband
    CHECK_FALSE(map.accept(0, 2055));        // +7 counts cumulative: still inside
    CHECK(map.accept(0, 2056));              // +8 counts: sent

    // Drifting just under the threshold each time never sends (compare to the
    // last *sent* value, not the last sample).
    CHECK_FALSE(map.accept(0, 2059)); // +3 from 2056 (last sent): inside
    CHECK_FALSE(map.accept(0, 2063)); // +7 from 2056 again, not from 2059
    CHECK(map.accept(0, 2064));       // +8 from 2056: sent
}

TEST_CASE("FaderMap channels are independent", "[control_surface]")
{
    FaderMap map;
    CHECK(map.accept(0, 100));
    CHECK(map.accept(1, 102)); // different channel: first sample sends
    CHECK_FALSE(map.accept(0, 105));
    CHECK_FALSE(map.accept(1, 108)); // +6 from its own last-sent value
}

TEST_CASE("FaderMap resetDeadband forces the next sample to send", "[control_surface]")
{
    FaderMap map;
    CHECK(map.accept(2, 3000));
    map.resetDeadband();
    CHECK(map.accept(2, 3001)); // same-ish value after a mode flip re-sends
}

TEST_CASE("FaderMap normalize maps 12-bit counts to 0..1", "[control_surface]")
{
    CHECK(FaderMap::normalize(0) == Catch::Approx(0.0f).margin(0.0001f));
    CHECK(FaderMap::normalize(4095) == Catch::Approx(1.0f).margin(0.0001f));
    CHECK(FaderMap::normalize(2048) == Catch::Approx(0.5f).margin(0.001f));
}
