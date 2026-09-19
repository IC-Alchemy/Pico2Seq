// Regression suite for edit publication: how stored sequencer lanes reach a
// sounding voice through VoiceEdit::composeLane playback mapping, and the
// contracts of refreshVoiceParameters() / refreshVoiceParametersAt()
// (no retrigger, no gate change, queued control updates drained by rendering).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sequencer/SequencerDefs.h"
#include "sequencer/Sequencer.h"
#include "voice/VoiceConfig.h"
#include "voice/VoiceEditParameters.h"
#include "voice/VoiceManager.h"
#include "voice/VoicePresets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

using namespace VoiceEdit;
using Catch::Approx;

namespace
{
// Real VoiceManager + Sequencer wired together through the patch playback
// transform, mirroring test_voice_edit.cpp's rmsWhileEditing rig.
class PlaybackRig
{
public:
    explicit PlaybackRig(uint8_t presetIndex)
    {
        config = VoicePresets::getPresetConfig(presetIndex);
        enablePatch(config);
        manager.init(48000.0f);
        voiceId = manager.addVoice(config);
        seq.setPlaybackTransform(composeLane, manager.getVoiceConfig(voiceId),
                                 mapOctave);
        seedModifiers(seq);
        seq.setStepParameterValue(ParamId::Gate, 0, 1);
        seq.setStepParameterValue(ParamId::GateLength, 0, 1.0f);
        seq.start();
        manager.setTransportMuted(false);
        // Let the staged patch apply (it waits for a low gate) before gate-on.
        for (int i = 0; i < 480; ++i)
            (void)manager.processAllVoices();
    }

    // Gate step 0 on; the retrigger belongs to this push and is consumed by
    // the caller's first render.
    void gateOn(uint32_t uclock = 0)
    {
        seq.advanceStep(uclock, -1, false, false, false, false, false, false,
                        -1, &state);
        manager.updateVoiceState(voiceId, state);
        state.shouldRetrigger = false;
    }

    // Refresh a sounding voice from the stored lanes and push it. Voice
    // renders consume one queued ControlUpdate per process call, so callers
    // render at least one sample after this before asserting.
    void refresh()
    {
        seq.refreshVoiceParameters(&state);
        manager.updateVoiceState(voiceId, state);
    }

    void skip(int samples)
    {
        for (int i = 0; i < samples; ++i)
            (void)manager.processAllVoices();
    }

    double rms(int samples)
    {
        double sum = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            const double y = manager.processAllVoices();
            sum += y * y;
        }
        return std::sqrt(sum / static_cast<double>(samples > 0 ? samples : 1));
    }

    VoiceManager manager{1};
    VoiceConfig config{};
    uint8_t voiceId = 0;
    Sequencer seq{};
    VoiceState state{};
};

// RMS of one gated step while `edit` runs every millisecond, exactly like the
// live lidar/fader/encoder loop through refreshVoiceParameters().
double rmsWithPeriodicRefresh(uint8_t preset, void (*edit)(Sequencer &, VoiceState &))
{
    PlaybackRig rig(preset);
    rig.gateOn();
    double sum = 0.0;
    constexpr int kSamples = 4800;
    for (int i = 0; i < kSamples; ++i)
    {
        if (i % 48 == 47)
        {
            edit(rig.seq, rig.state);
            rig.refresh();
        }
        const double y = rig.manager.processAllVoices();
        sum += y * y;
    }
    return std::sqrt(sum / static_cast<double>(kSamples));
}
} // namespace

TEST_CASE("Neutral modifiers compose every preset back to its lane bases",
          "[edit_publication]")
{
    for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset)
    {
        VoiceConfig config = VoicePresets::getPresetConfig(preset);
        enablePatch(config);
        INFO(VoicePresets::getPresetName(preset));

        Sequencer seq;
        seedModifiers(seq);
        seq.setPlaybackTransform(composeLane, &config, mapOctave);
        seq.setStepParameterValue(ParamId::Gate, 0, 1);
        seq.start();
        VoiceState state;
        seq.advanceStep(0, -1, false, false, false, false, false, false, -1, &state);

        // A gated step plays the patch bases: neutral modifiers sit exactly
        // on each lane's base as defined by laneBase().
        REQUIRE(state.isGateHigh);
        CHECK(state.velocityLevel ==
              Approx(laneBase(ParamId::Velocity, config)).margin(1e-4));
        CHECK(state.filterCutoff ==
              Approx(laneBase(ParamId::Filter, config)).margin(1e-4));
        CHECK(state.attackTimeSeconds ==
              Approx(laneBase(ParamId::Attack, config)).margin(1e-4));
        CHECK(state.decayTimeSeconds ==
              Approx(laneBase(ParamId::Decay, config)).margin(1e-4));
        CHECK(state.noteIndex ==
              Approx(composeLane(ParamId::Note, 0.0f, &config)).margin(1e-4));
        CHECK(state.octaveOffset == static_cast<int8_t>(config.baseOctave));
        CHECK(state.gateLengthTicks ==
              static_cast<uint16_t>(
                  std::max(1.0f, config.baseGateLength * 120.0f)));

        // The composed read-back of the same step agrees with the voice state.
        const Step composed = seq.getPlaybackStep(0);
        CHECK(composed.velocityLevel == state.velocityLevel);
        CHECK(composed.filterCutoff == state.filterCutoff);
        CHECK(composed.attackTimeSeconds == state.attackTimeSeconds);
        CHECK(composed.decayTimeSeconds == state.decayTimeSeconds);
        CHECK(composed.noteIndex == state.noteIndex);
        CHECK(composed.octaveOffset == state.octaveOffset);
    }
}

TEST_CASE("Stored filter endpoints survive a shifted patch base via getPlaybackStep",
          "[edit_publication]")
{
    VoiceConfig config = VoicePresets::getSquareVoice();
    enablePatch(config);
    Sequencer seq;
    seedModifiers(seq);
    seq.setPlaybackTransform(composeLane, &config, mapOctave);
    seq.setStepParameterValue(ParamId::Gate, 0, 1);

    // Endpoints are absolute regardless of the base...
    seq.setStepParameterValue(ParamId::Filter, 0, 0.0f);
    CHECK(seq.getPlaybackStep(0).filterCutoff == 0.0f);
    seq.setStepParameterValue(ParamId::Filter, 0, 1.0f);
    CHECK(seq.getPlaybackStep(0).filterCutoff == 1.0f);

    // ...and the neutral modifier always reproduces the current base.
    seq.setStepParameterValue(ParamId::Filter, 0, 0.5f);
    CHECK(seq.getPlaybackStep(0).filterCutoff ==
          Approx(config.filterCutoffBase).margin(1e-4));

    // Shifting the base moves the neutral step with it, endpoints stay pinned.
    config.filterCutoffBase = 0.3f;
    CHECK(seq.getPlaybackStep(0).filterCutoff == Approx(0.3f).margin(1e-4));
    seq.setStepParameterValue(ParamId::Filter, 0, 0.0f);
    CHECK(seq.getPlaybackStep(0).filterCutoff == 0.0f);
    seq.setStepParameterValue(ParamId::Filter, 0, 1.0f);
    CHECK(seq.getPlaybackStep(0).filterCutoff == 1.0f);
}

TEST_CASE("Live parameter edits refresh a sounding note without gating or retriggering",
          "[edit_publication]")
{
    const uint8_t square = static_cast<uint8_t>(VoicePresets::findPreset("Square"));

    // Editing every millisecond like the live control loop must sound like no
    // edit at all, and a real stored-lane edit must not drop near-silent (the
    // old previewActiveStep path retriggered and halved the output; a
    // retriggered voice is trapped in the first samples of its attack). The
    // edited lane is Velocity upward, so a healthy publication can only get
    // louder while a retriggering one collapses.
    const double untouched =
        rmsWithPeriodicRefresh(square, [](Sequencer &, VoiceState &) {});
    const double refreshed = rmsWithPeriodicRefresh(
        square, [](Sequencer &s, VoiceState &v) { s.refreshVoiceParameters(&v); });
    const double edited = rmsWithPeriodicRefresh(square, [](Sequencer &s, VoiceState &v) {
        s.setStepParameterValue(ParamId::Velocity, 0, 0.75f);
        s.refreshVoiceParameters(&v);
    });
    REQUIRE(untouched > 0.01);
    CHECK(refreshed == Approx(untouched).epsilon(0.05));
    CHECK(edited > untouched);          // composed 0.75 over base 0.5: audible
    REQUIRE(edited > untouched * 0.5); // and never retriggered to near-silence

    // Contract on the shared state: refresh publishes values in place and
    // never touches the gate or the envelope trigger.
    PlaybackRig rig(square);
    rig.gateOn();
    REQUIRE(rig.state.isGateHigh);
    rig.seq.setStepParameterValue(ParamId::Velocity, 0, 0.7f);
    rig.refresh();
    CHECK(rig.state.isGateHigh);
    CHECK_FALSE(rig.state.shouldRetrigger);
    CHECK(rig.state.velocityLevel == Approx(0.7f).margin(1e-4)); // base 0.5
    CHECK(rig.seq.getStepParameterValue(ParamId::Velocity, 0) == Approx(0.7f));
}

TEST_CASE("refreshVoiceParametersAt previews one composed step without retriggering",
          "[edit_publication]")
{
    VoiceConfig config = VoicePresets::getSquareVoice();
    enablePatch(config);
    Sequencer seq;
    seedModifiers(seq);
    seq.setPlaybackTransform(composeLane, &config, mapOctave);
    seq.setStepParameterValue(ParamId::Gate, 0, 1);
    seq.setStepParameterValue(ParamId::Velocity, 5, 0.25f);
    seq.setStepParameterValue(ParamId::Filter, 5, 1.0f);

    VoiceState state;
    state.isGateHigh = false;  // stopped preview: no sounding note to protect
    state.noteIndex = 42.0f;   // sentinel: pitch must be overwritten regardless
    state.octaveOffset = -12;
    state.shouldRetrigger = true;
    seq.refreshVoiceParametersAt(5, &state);

    const Step expected = seq.getPlaybackStep(5);
    CHECK(state.velocityLevel == expected.velocityLevel);
    CHECK(state.filterCutoff == expected.filterCutoff);
    CHECK(state.attackTimeSeconds == expected.attackTimeSeconds);
    CHECK(state.decayTimeSeconds == expected.decayTimeSeconds);
    CHECK(state.noteIndex == expected.noteIndex);
    CHECK(state.octaveOffset == expected.octaveOffset);
    // Pitch is set unconditionally while stopped, and no retrigger escapes.
    CHECK(state.noteIndex == Approx(config.baseNote).margin(1e-4));
    CHECK(state.octaveOffset == static_cast<int8_t>(config.baseOctave));
    CHECK_FALSE(state.shouldRetrigger);
    // The composed values come from the stored lanes through the patch bases.
    CHECK(state.velocityLevel ==
          Approx(composeLane(ParamId::Velocity, 0.25f, &config)).margin(1e-4));
    CHECK(state.filterCutoff == 1.0f);
}

TEST_CASE("Stored Filter endpoints give a settled loudness difference beyond 4x",
          "[edit_publication]")
{
    // Analog: ladder low-pass (150 Hz..6 kHz octave taper), envelope on, so
    // the low-pass makes the endpoints strictly ordered.
    const uint8_t analog = static_cast<uint8_t>(VoicePresets::findPreset("Analog"));
    auto settledRms = [&](float storedFilter) {
        PlaybackRig rig(analog);
        rig.seq.setStepParameterValue(ParamId::Filter, 0, storedFilter);
        rig.gateOn();
        rig.skip(24000); // settle the ADSR and cutoff smoothing
        return rig.rms(24000);
    };
    const double closed = settledRms(0.0f); // composed 0.0 -> 150 Hz low-pass
    const double open = settledRms(1.0f);   // composed 1.0 -> 6 kHz low-pass
    REQUIRE(std::isfinite(closed));
    REQUIRE(std::isfinite(open));
    REQUIRE(open > 0.01);
    REQUIRE(open > closed * 4.0); // the sequenced cutoff is clearly audible
}

TEST_CASE("Velocity lane amplitude roughly tracks stored step values",
          "[edit_publication]")
{
    // Square keeps PARAMSET_STANDARD, so the composed velocity scales the VCA.
    const uint8_t square = static_cast<uint8_t>(VoicePresets::findPreset("Square"));
    auto velocityRms = [&](float storedVelocity) {
        PlaybackRig rig(square);
        rig.seq.setStepParameterValue(ParamId::Velocity, 0, storedVelocity);
        rig.gateOn();
        return rig.rms(4800);
    };
    const double weak = velocityRms(0.05f);  // composed 0.05
    const double strong = velocityRms(0.9f); // composed 0.9
    REQUIRE(std::isfinite(weak));
    REQUIRE(std::isfinite(strong));
    REQUIRE(weak > 0.0);
    // 18x stored travel must stay clearly ordered (identical envelope shape).
    REQUIRE(strong > weak * 4.0);
}

TEST_CASE("Attack lane shapes the first energy after gate-on", "[edit_publication]")
{
    const uint8_t square = static_cast<uint8_t>(VoicePresets::findPreset("Square"));
    auto earlyEnergy = [&](float storedAttack) {
        PlaybackRig rig(square);
        rig.seq.setStepParameterValue(ParamId::Attack, 0, storedAttack);
        rig.gateOn();
        return rig.rms(2400); // first 50 ms after gate-on
    };
    const double quick = earlyEnergy(0.0f); // composed 0.0 -> 1 ms attack
    const double slow = earlyEnergy(1.0f);  // composed 1.0 -> 2 s attack
    REQUIRE(std::isfinite(quick));
    REQUIRE(std::isfinite(slow));
    REQUIRE(quick > slow * 4.0); // the short attack spends its energy early
}

TEST_CASE("Decay lane changes land at the next gate-on without a mid-note retrigger",
          "[edit_publication]")
{
    const uint8_t square = static_cast<uint8_t>(VoicePresets::findPreset("Square"));
    // Square decays to a zero sustain, so a late window (0.5 s..1.0 s after
    // gate-on) is loud only under the long decay. An already-finished
    // envelope segment is NOT required to restart when its duration changes
    // mid-note; the change must simply publish without retriggering and be
    // reflected by the next gate-on.
    auto lateWindowRms = [&](bool applyLongMidGate) {
        PlaybackRig rig(square);
        rig.seq.setStepParameterValue(ParamId::Decay, 0, 0.0f); // 1 ms decay
        rig.gateOn();
        rig.skip(2400);
        if (applyLongMidGate)
        {
            rig.seq.setStepParameterValue(ParamId::Decay, 0, 1.0f); // 10 s decay
            rig.refresh();
            CHECK(rig.state.isGateHigh);
            CHECK_FALSE(rig.state.shouldRetrigger); // publishing never retriggers
            rig.skip(2400); // drain the queued update
        }
        rig.gateOn(16); // next gate-on: Gate lane length 16 -> step 0 again
        rig.skip(24000);
        return rig.rms(24000);
    };
    const double stillShort = lateWindowRms(false);
    const double longApplied = lateWindowRms(true);
    REQUIRE(std::isfinite(stillShort));
    REQUIRE(std::isfinite(longApplied));
    CHECK(stillShort < 0.002);  // decayed to silence before the late window
    REQUIRE(longApplied > 0.01);
    REQUIRE(longApplied > stillShort * 4.0);
}

TEST_CASE("Hard-sync slave pitch tracks the Velocity lane at a stable loudness",
          "[edit_publication]")
{
    // Analog is PARAMSET_HARDSYNC: the Velocity lane no longer scales the VCA
    // (velocityToAmplitude() is false there); around its neutral 0.5 it
    // offsets the hard-sync slave by +/- 24 semitones from the master. Metric
    // choice: the zero-crossing rate (with a small hysteresis band so filter
    // ripple cannot double-count) tracks the slave's faster/slower reset
    // cadence - a pitch-like count that loudness cannot fake - while the RMS
    // only has to stay in the same order, proving velocity moved pitch and
    // not amplitude.
    const uint8_t analog = static_cast<uint8_t>(VoicePresets::findPreset("Analog"));
    auto profile = [&](float storedVelocity) {
        PlaybackRig rig(analog);
        rig.seq.setStepParameterValue(ParamId::Velocity, 0, storedVelocity);
        rig.gateOn();
        rig.skip(4800); // settle pitch slew and filter
        constexpr int kSamples = 24000;
        int crossings = 0;
        int previousSign = 0;
        double sum = 0.0;
        for (int i = 0; i < kSamples; ++i)
        {
            const double y = rig.manager.processAllVoices();
            sum += y * y;
            const int sign = (y > 1.0e-4) - (y < -1.0e-4);
            if (sign != 0)
            {
                if (previousSign != 0 && sign != previousSign)
                    ++crossings;
                previousSign = sign;
            }
        }
        return std::make_pair(static_cast<double>(crossings) / kSamples,
                              std::sqrt(sum / kSamples)); // (zero-crossing rate, rms)
    };
    const auto centered = profile(0.5f); // 1:1 sync: plain master saw
    const auto high = profile(0.9f);     // slave ~+19 semitones up
    const auto low = profile(0.1f);      // slave ~-19 semitones down
    INFO("centered zcr " << centered.first << " rms " << centered.second);
    INFO("high zcr " << high.first << " rms " << high.second);
    INFO("low zcr " << low.first << " rms " << low.second);
    REQUIRE(centered.first > 0.0);
    REQUIRE(centered.second > 0.005);
    CHECK(high.first > centered.first * 1.3); // faster slave: more crossings
    // Falling velocity never RAISES the slave pitch. (A deep negative offset
    // can leave the crossing rate master-dominated — the slave never completes
    // a cycle before the master resets it — so only monotonicity is asserted
    // on this side; the high-side ratio carries the pitch-not-amplitude proof.)
    CHECK(low.first <= centered.first * 1.001f);
    // Amplitude stays the same order while the pitch moves.
    CHECK(high.second < centered.second * 4.0);
    CHECK(low.second > centered.second * 0.25);
}

TEST_CASE("Queued control updates drain one rendered sample at a time",
          "[edit_publication]")
{
    const uint8_t square = static_cast<uint8_t>(VoicePresets::findPreset("Square"));
    PlaybackRig rig(square);
    REQUIRE(rig.manager.getVoiceState(rig.voiceId) != nullptr);

    // Two updates are queued back to back: the gate-on push and a lane
    // refresh. Voice::process consumes one per rendered sample, so render at
    // least one sample per queued update before asserting anything.
    rig.gateOn();
    rig.seq.setStepParameterValue(ParamId::Filter, 0, 1.0f);
    rig.refresh();
    rig.skip(2);

    // The requested control state carries the latest push...
    const VoiceState *requested = rig.manager.getVoiceState(rig.voiceId);
    REQUIRE(requested != nullptr);
    CHECK(requested->filterCutoff == Approx(1.0f));
    CHECK(requested->isGateHigh);
    CHECK_FALSE(rig.state.shouldRetrigger);

    // ...and once drained the voice actually sounds with the new value.
    const double audible = rig.rms(2400);
    REQUIRE(std::isfinite(audible));
    REQUIRE(audible > 0.01);
}
