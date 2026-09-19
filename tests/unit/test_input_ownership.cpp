// Input ownership regression suite (tests/unit/test_input_ownership.cpp).
//
// Deterministic ownership order between manual edits and the lidar, at the
// policy level (ControlSurface::StepEditOwnership and ControlSurface::EncoderMotion):
//
//   1. A manual edit — an encoder turn or an accepted fader move — lands on
//      exactly ONE voice/parameter/step and takes ownership via take().
//   2. From that moment, an automatic lidar write is suppressed ONLY for the
//      exact owned target; every other voice/param/step keeps recording from
//      the sensor (so a stationary valid hand cannot replace the manual value
//      on the next control pass, but cannot block other lanes either).
//   3. The suppression is stateless: polling suppressesLidar() any number of
//      times keeps it true; there is no decay.
//   4. It rearms (lidar enabled again) when the hand leaves the sensor window
//      (rearmLidar) while the owned target fields are kept, and a second
//      take() re-suppresses.
//   5. Target changes route through reset(): UITransitions::clearStepEdit,
//      voice changes, focus changes and mode/modal editor changes all call
//      it, dropping the target entirely.
//   6. EncoderMotion carries slow turns between sensor reads so an increment
//      below the noise floor still amounts to a change — the accumulation,
//      detent and reversal rules behind when a manual edit "lands" at all.

#include "ui/ControlSurfaceLogic.h"
#include "ui/UITransitions.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace ControlSurface;

namespace
{
constexpr uint8_t kVoice = 1;
constexpr uint8_t kParam = static_cast<uint8_t>(ParamId::Filter);
constexpr uint8_t kStep = 7;

// Same encoder noise floor / detent scale the bridge uses.
constexpr float kNoiseFloor = 0.0005f;
constexpr float kDetent = 0.03f;
} // namespace

// ---------------------------------------------------------------------------
// Ownership scope: the exact target, and nothing else
// ---------------------------------------------------------------------------

TEST_CASE("A manual edit suppresses lidar only for its exact owned target",
          "[input_ownership]")
{
    StepEditOwnership owner;
    REQUIRE_FALSE(owner.active());

    // The manual edit lands: one voice + parameter + step.
    owner.take(kVoice, kParam, kStep);
    REQUIRE(owner.active());
    REQUIRE(owner.voice() == kVoice);
    REQUIRE(owner.param() == kParam);
    REQUIRE(owner.step() == kStep);

    // Same target: suppressed — the stationary hand must not win.
    REQUIRE(owner.suppressesLidar(kVoice, kParam, kStep));

    // A different step of the same voice and lane keeps recording.
    REQUIRE_FALSE(owner.suppressesLidar(kVoice, kParam, kStep + 1));
    // A different parameter on the same voice and step keeps recording.
    REQUIRE_FALSE(owner.suppressesLidar(kVoice,
                                        static_cast<uint8_t>(ParamId::Attack),
                                        kStep));
    // A different voice on the same parameter and step keeps recording.
    REQUIRE_FALSE(owner.suppressesLidar(kVoice + 1, kParam, kStep));
}

TEST_CASE("rearmLidar keeps the owned target and a new manual edit re-suppresses",
          "[input_ownership]")
{
    StepEditOwnership owner;
    owner.take(kVoice, kParam, kStep);
    REQUIRE(owner.suppressesLidar(kVoice, kParam, kStep));

    // The hand left the sensor window: lidar re-enables...
    owner.rearmLidar();
    REQUIRE_FALSE(owner.suppressesLidar(kVoice, kParam, kStep));
    // ...but the ownership target fields are kept for the next suppression.
    REQUIRE(owner.active());
    REQUIRE(owner.voice() == kVoice);
    REQUIRE(owner.param() == kParam);
    REQUIRE(owner.step() == kStep);

    // A second manual edit on the (kept) target re-suppresses lidar.
    owner.take(kVoice, kParam, kStep);
    REQUIRE(owner.suppressesLidar(kVoice, kParam, kStep));

    // A new edit that moves the target re-suppresses at the NEW target and
    // releases the old one.
    owner.rearmLidar();
    const uint8_t kOtherStep = 3;
    owner.take(kVoice, kParam, kOtherStep);
    REQUIRE(owner.suppressesLidar(kVoice, kParam, kOtherStep));
    REQUIRE_FALSE(owner.suppressesLidar(kVoice, kParam, kStep));
    REQUIRE(owner.step() == kOtherStep);
}

TEST_CASE("reset drops the owned target entirely", "[input_ownership]")
{
    // Every target change routes here: UITransitions::clearStepEdit (step
    // exit, slide toggle, settings open/close), voice changes, focus changes
    // and mode/modal editor changes.
    StepEditOwnership owner;
    owner.take(kVoice, kParam, kStep);
    REQUIRE(owner.suppressesLidar(kVoice, kParam, kStep));

    owner.reset();
    REQUIRE_FALSE(owner.active());
    REQUIRE(owner.voice() == StepEditOwnership::kNoVoice);
    REQUIRE_FALSE(owner.suppressesLidar(kVoice, kParam, kStep));

    // The UIState route: UITransitions::clearStepEdit owns stepEditOwner
    // resets exactly the same way.
    UIState state;
    state.stepEditOwner.take(kVoice, kParam, kStep);
    state.selectedStepForEdit = kStep;
    state.currentEditParameter = static_cast<ParamId>(kParam);
    REQUIRE(state.stepEditOwner.active());

    UITransitions::clearStepEdit(state);
    REQUIRE(state.selectedStepForEdit == -1);
    REQUIRE(state.currentEditParameter == ParamId::Count);
    REQUIRE_FALSE(state.stepEditOwner.active());
    REQUIRE_FALSE(state.stepEditOwner.suppressesLidar(kVoice, kParam, kStep));
}

TEST_CASE("Suppression is stateless: polling a stationary hand never decays it",
          "[input_ownership]")
{
    StepEditOwnership owner;
    owner.take(kVoice, kParam, kStep);

    // The lidar pass runs every control tick and re-asks each time. There is
    // no counter and no timeout: repeated stateless checks stay suppressed.
    for (int pass = 0; pass < 256; ++pass)
    {
        CAPTURE(pass);
        REQUIRE(owner.suppressesLidar(kVoice, kParam, kStep));
    }

    // Interleaved passes for other targets stay unsuppressed the whole time.
    for (int pass = 0; pass < 64; ++pass)
    {
        REQUIRE_FALSE(owner.suppressesLidar(kVoice, kParam, kStep + 1));
        REQUIRE(owner.suppressesLidar(kVoice, kParam, kStep));
    }
}

// ---------------------------------------------------------------------------
// EncoderMotion: when a manual edit counts at all
// ---------------------------------------------------------------------------

TEST_CASE("Slow encoder turns accumulate until they cross the noise floor",
          "[input_ownership]")
{
    // A slow turn yields less than the noise floor per sensor read; the
    // pending motion must be carried, not discarded, until it amounts to a
    // continuous change (and only then does it land as a manual edit).
    EncoderMotion motion;
    const float slowRead = kNoiseFloor / 10.0f;
    for (int read = 0; read < 9; ++read)
    {
        motion.add(slowRead);
        REQUIRE(motion.takeContinuous(kNoiseFloor) == 0.0f);
    }
    // The tenth read crosses the floor: all pending motion is delivered once.
    motion.add(2.0f * slowRead);
    REQUIRE(motion.takeContinuous(kNoiseFloor) == Catch::Approx(1.1f * kNoiseFloor));
    REQUIRE(motion.pending() == 0.0f);
    REQUIRE(motion.takeContinuous(kNoiseFloor) == 0.0f);
}

TEST_CASE("Stepped encoder edits consume whole detents and keep the remainder",
          "[input_ownership]")
{
    EncoderMotion motion;

    // Below one detent: no step yet, motion kept.
    motion.add(kDetent * 0.5f);
    REQUIRE(motion.takeSteps(kDetent) == 0);
    REQUIRE(motion.pending() == Catch::Approx(kDetent * 0.5f));

    // Crossing one detent: exactly one step, remainder stays pending.
    motion.add(kDetent * 0.6f);
    REQUIRE(motion.takeSteps(kDetent) == 1);
    REQUIRE(motion.pending() == Catch::Approx(kDetent * 0.1f).margin(1e-6f));

    // A fast twist passing several detents in one read steps once per detent.
    motion.add(kDetent * 2.5f);
    REQUIRE(motion.takeSteps(kDetent) == 2);

    // Negative direction truncates toward zero and keeps its remainder too.
    motion.reset();
    motion.add(-kDetent * 3.2f);
    REQUIRE(motion.takeSteps(kDetent) == -3);
    REQUIRE(motion.pending() == Catch::Approx(-kDetent * 0.2f).margin(1e-6f));
}

TEST_CASE("A direction change discards pending encoder motion", "[input_ownership]")
{
    // Sensor jitter alternates direction; it must never add up to a change,
    // and a real reversal must respond without unwinding the old direction.
    EncoderMotion motion;
    motion.add(kNoiseFloor * 0.9f);
    motion.add(-kNoiseFloor * 0.1f);
    REQUIRE(motion.pending() == Catch::Approx(-kNoiseFloor * 0.1f));

    for (int read = 0; read < 1000; ++read)
    {
        motion.add((read % 2 == 0 ? 1.0f : -1.0f) * kNoiseFloor * 0.6f);
        REQUIRE(motion.takeContinuous(kNoiseFloor) == 0.0f);
    }
}

TEST_CASE("Zero and non-finite encoder increments never become edits",
          "[input_ownership]")
{
    EncoderMotion motion;
    motion.add(0.01f);
    motion.add(0.0f);
    motion.add(std::nanf(""));
    motion.add(-std::nanf(""));
    motion.add(std::numeric_limits<float>::infinity());
    motion.add(-std::numeric_limits<float>::infinity());
    // Only the finite, non-zero increment survived.
    REQUIRE(motion.pending() == Catch::Approx(0.01f));

    // Degenerate detent sizes take no steps and leave the pending motion.
    REQUIRE(motion.takeSteps(0.0f) == 0);
    REQUIRE(motion.takeSteps(-kDetent) == 0);
    REQUIRE(motion.pending() == Catch::Approx(0.01f));
}
