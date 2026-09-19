#include "src/ui/VoiceEditControls.h"
#include "src/voice/VoiceEditParameters.h"
#include "src/voice/VoiceManager.h"
#include "src/voice/VoicePresets.h"
#include "src/voice/MusicalValues.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>
using Catch::Approx;
using namespace VoiceEdit;

TEST_CASE(
    "Live lidar recording stores the absolute value the step plays",
    "[voice_edit][recording]") {
  auto config = VoicePresets::getDigitalVoice();
  enablePatch(config);
  config.baseVelocity = 0.6f;
  Sequencer seq;
  seedModifiers(seq);
  seq.setPlaybackTransform(composeLane, &config, mapOctave);
  seq.setStepParameterValue(ParamId::Gate, 0, 1);
  seq.start();
  seq.setRecordingInput(0.75f);
  VoiceState state;
  seq.advanceStep(0, 100, false, true, false, false, false, false, -1, &state);
  const auto step = seq.getCurrentStepForParameter(ParamId::Velocity);
  REQUIRE(seq.getStepParameterValue(ParamId::Velocity, step) == 0.75f);
  REQUIRE(state.velocityLevel == Approx(0.75f));
  REQUIRE(config.baseVelocity == Approx(0.6f));
  // A recorded step keeps its value when the patch moves.
  config.baseVelocity = 0.4f;
  seq.playStepNow(step, &state);
  REQUIRE(state.velocityLevel == Approx(0.75f));
  REQUIRE(seq.getStepParameterValue(ParamId::Velocity, step) == 0.75f);
  // A cleared step follows the patch again.
  seq.resetModifierStep(step);
  REQUIRE(followsPatch(seq.getStepParameterValue(ParamId::Velocity, step)));
  REQUIRE(seq.getStepParameterValue(ParamId::Note, step) == 0);
  REQUIRE(seq.getStepParameterValue(ParamId::Gate, step) == 0);
  seq.setStepParameterValue(ParamId::Gate, step, 1);
  seq.playStepNow(step, &state);
  REQUIRE(state.velocityLevel == Approx(0.4f));
  REQUIRE(config.baseVelocity == Approx(0.4f));
}

TEST_CASE(
    "Hard-sync and recipe aliases edit one shared base with musical units",
    "[voice_edit]") {
  auto sync = VoicePresets::getAnalogVoice();
  enablePatch(sync);
  setValue(Id::Velocity, sync, 12);
  REQUIRE(sync.baseVelocity == Approx(0.75f));
  REQUIRE(value(Id::Velocity, sync) == Approx(12));
  REQUIRE(composeLane(ParamId::Velocity, SequencerConstants::LANE_FOLLOWS_PATCH, &sync) == Approx(0.75f));
  REQUIRE(composeLane(ParamId::Velocity, 0.25f, &sync) == Approx(0.25f));
  setValue(Id::Engine, sync, ENGINE_HYPERSAW);
  setValue(Id::Engine, sync, ENGINE_OSC);
  REQUIRE(sync.paramSet == PARAMSET_HARDSYNC);
  setValue(Id::Wave1, sync, 0);
  REQUIRE(sync.paramSet == PARAMSET_STANDARD);
  auto wg = VoicePresets::getWaveguidePluckVoice();
  enablePatch(wg);
  setValue(Id::Decay, wg, 4);
  REQUIRE(value(Id::T60, wg) == Approx(4));
  setValue(Id::T60, wg, 2);
  REQUIRE(value(Id::Decay, wg) == Approx(2));
}

TEST_CASE("Patch bases and recorded step values are independent",
          "[voice_edit]") {
  VoiceConfig config = VoicePresets::getDigitalVoice();
  enablePatch(config);
  Sequencer seq;
  seedModifiers(seq);
  seq.setPlaybackTransform(composeLane, &config, mapOctave);
  seq.setStepParameterValue(ParamId::Gate, 0, 1);
  REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 0);
  VoiceState state;
  seq.playStepNow(0, &state);
  REQUIRE(state.noteIndex == config.baseNote);
  REQUIRE(state.velocityLevel == Approx(config.baseVelocity));
  REQUIRE(state.octaveOffset == 0);
  REQUIRE(state.gateLengthTicks ==
          static_cast<uint16_t>(config.baseGateLength * 120));
  setValue(Id::Note, config, 12);
  setValue(Id::Velocity, config, 0.7f);
  seq.playStepNow(0, &state);
  REQUIRE(state.noteIndex == 12);
  REQUIRE(state.velocityLevel == Approx(0.7f));
  REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 0);
  REQUIRE(followsPatch(seq.getStepParameterValue(ParamId::Velocity, 0)));
  seq.setStepParameterValue(ParamId::Velocity, 0, 0.25f);
  seq.playStepNow(0, &state);
  REQUIRE(state.velocityLevel == Approx(0.25f));
  REQUIRE(config.baseVelocity == Approx(0.7f));
  setValue(Id::GateLength, config, 0.8f);
  seq.playStepNow(0, &state);
  REQUIRE(state.gateLengthTicks == 96);
  setValue(Id::Gate, config, 0);
  seq.playStepNow(0, &state);
  REQUIRE_FALSE(state.isGateHigh);
  REQUIRE(seq.getStepParameterValue(ParamId::Gate, 0) == 1);
}

TEST_CASE("Every preset plays its sound bases on steps that follow the patch",
          "[voice_edit]") {
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    auto c = VoicePresets::getPresetConfig(preset);
    enablePatch(c);
    INFO(VoicePresets::getPresetName(preset));
    for (ParamId lane : {ParamId::Velocity, ParamId::Filter, ParamId::Attack,
                         ParamId::Decay, ParamId::Sustain, ParamId::Release}) {
      const auto &b = VoiceParameters::binding(c, lane);
      const float composed = composeLane(lane, SequencerConstants::LANE_FOLLOWS_PATCH, &c);
      if (b.target)
        REQUIRE(b.map(composed) == Approx(c.*(b.target)).margin(0.00001));
      REQUIRE(composeLane(lane, 0, &c) >= 0);
      REQUIRE(composeLane(lane, 1, &c) <= 1);
    }
    Sequencer seq;
    seedModifiers(seq);
    seq.setPlaybackTransform(composeLane, &c, mapOctave);
    seq.setStepParameterValue(ParamId::Gate, 0, 1);
    VoiceState state;
    seq.playStepNow(0, &state);
    Voice voice(0, c);
    voice.init(48000);
    voice.updateParameters(state);
    for (int i = 0; i < 512; ++i)
      REQUIRE(std::isfinite(voice.process()));
    REQUIRE(voice.getRequestedConfig().defaultRelease == c.defaultRelease);
  }
}

TEST_CASE("Parameter catalogue is reachable and bounded for every engine",
          "[voice_edit]") {
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    auto c = VoicePresets::getPresetConfig(preset);
    enablePatch(c);
    c.hasFilter = c.hasEnvelope = c.hasOverdrive = true;
    INFO(VoicePresets::getPresetName(preset));
    std::set<Id> reachable;
    Id group = Id::Note;
    for (int g = 0; g < static_cast<int>(Group::Count); ++g) {
      Id cursor = group;
      for (int n = 0; n < static_cast<int>(Id::Count); ++n) {
        if (available(cursor, c))
          reachable.insert(cursor);
        cursor = nextParameter(cursor, 1, c, false);
      }
      group = nextParameter(group, 1, c, true);
    }
    for (int i = 0; i < static_cast<int>(Id::Count); ++i) {
      const auto id = static_cast<Id>(i);
      if (!available(id, c))
        continue;
      INFO(name(id, c));
      REQUIRE(reachable.count(id) == 1);
      char text[24];
      format(id, c, text, sizeof(text));
      REQUIRE(std::strlen(text) > 0);
      auto edited = c;
      adjust(id, edited, 100);
      REQUIRE(std::isfinite(value(id, edited)));
      adjust(id, edited, -100);
      REQUIRE(std::isfinite(value(id, edited)));
    }
  }
  VoiceConfig c;
  setValue(Id::Engine, c, ENGINE_RECIPE);
  for (int i = 0; i <= static_cast<int>(parameter(Id::Recipe).maximum); ++i) {
    setValue(Id::Recipe, c, i);
    REQUIRE(c.recipe != nullptr);
    REQUIRE(c.parameters != nullptr);
  }
  setValue(Id::Wave1, c, 7);
  REQUIRE(c.oscWaveforms[0] == WAVE_NOISE);
  c.filterType = FILTER_SVF;
  setValue(Id::FilterMode, c, 2);
  REQUIRE(c.filterMode == VoiceFilterMode::HP24);
}

TEST_CASE("Every preset recipe survives an editor selection round trip", "[voice_edit][recipes]") {
  for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
    auto c = VoicePresets::getPresetConfig(p);
    if (c.engine != ENGINE_RECIPE) continue;
    INFO(VoicePresets::getPresetName(p));
    const auto *recipe = c.recipe;
    const auto *layout = c.parameters;
    const float selected = value(Id::Recipe, c);
    setValue(Id::Recipe, c, selected);
    REQUIRE(c.recipe == recipe);
    REQUIRE(c.parameters == layout);
  }
}

TEST_CASE("Editor requires release and never replays held actions",
          "[voice_edit]") {
  Controls controls;
  controls.enter();
  REQUIRE_FALSE(controls.poll(128, 8, 0).exit);
  REQUIRE(controls.poll(128, 0, 50).voice == -1);
  controls.poll(0, 0, 100);
  auto input = controls.poll(16, 4, 200);
  REQUIRE(input.voice == 2);
  REQUIRE(input.clearEncoder);
  REQUIRE(controls.fine);
  REQUIRE(controls.poll(16, 4, 250).voice == -1);
  controls.poll(0, 0, 300);
  controls.poll(32, 0, 400);
  REQUIRE_FALSE(controls.poll(32, 0, 1099).reset);
  REQUIRE(controls.poll(32, 0, 1100).reset);
  REQUIRE_FALSE(controls.poll(32, 0, 2000).reset);
  controls.poll(0, 0, 2100);
  controls.poll(32, 0, 2200);
  controls.poll(32, 1, 2300); // Changing target cancels the pending reset.
  REQUIRE_FALSE(controls.poll(32, 0, 3000).reset);
  REQUIRE(controls.poll(128, 0, 3100).exit);
}

TEST_CASE("Muted editor still drains queued changes for all four voices",
          "[voice_edit]") {
  VoiceManager manager(4);
  uint8_t ids[4];
  for (uint8_t i = 0; i < 4; ++i)
    ids[i] = manager.addVoice(VoicePresets::getAnalogVoice());
  manager.setTransportMuted(true);
  for (uint8_t i = 0; i < 4; ++i) {
    auto c = *manager.getVoiceConfig(ids[i]);
    c.hasEnvelope = false;
    c.baseVelocity = 0.1f * (i + 1);
    enablePatch(c);
    manager.setVoiceConfig(ids[i], c);
  }
  // Mute ramps rather than hard-cutting: keep processing so the queued config
  // changes drain while the ramp completes, then confirm silence.
  for (int i = 0; i < 1024; ++i)
    (void)manager.processAllVoices();
  for (int i = 0; i < 48000; ++i)
    (void)manager.processAllVoices();
  REQUIRE(std::abs(manager.processAllVoices()) < 1.0e-9f);
  for (uint8_t i = 0; i < 4; ++i)
    REQUIRE(manager.getVoiceConfig(ids[i])->baseVelocity ==
            Approx(0.1f * (i + 1)));
}


#include "src/voice/MusicalValues.h"
#include <string>

TEST_CASE("OLED snapshot matches playback and never triggers a note", "[voice_edit][oled]") {
  auto c = VoicePresets::getDigitalVoice();
  enablePatch(c);
  Sequencer seq;
  seedModifiers(seq);
  seq.setPlaybackTransform(composeLane, &c, mapOctave);
  seq.setParameterStepCount(ParamId::Note, 3);
  seq.setParameterStepCount(ParamId::Octave, 5);
  for (uint8_t i = 0; i < 16; ++i) seq.setStepParameterValue(ParamId::Gate, i, 1);
  for (uint8_t i = 0; i < 3; ++i) seq.setStepParameterValue(ParamId::Note, i, i + 1);
  seq.setStepParameterValue(ParamId::Octave, 4, 0.75f);
  seq.start();
  VoiceState state;
  seq.advanceStep(4, -1, false, false, false, false, false, false, -1, &state);
  const auto before = seq.getCurrentStep();
  const auto read = seq.getPlaybackStep();
  REQUIRE(read.noteIndex == state.noteIndex);
  REQUIRE(read.octaveOffset == state.octaveOffset);
  REQUIRE(read.velocityLevel == state.velocityLevel);
  REQUIRE(read.attackTimeSeconds == state.attackTimeSeconds);
  REQUIRE(read.decayTimeSeconds == state.decayTimeSeconds);
  REQUIRE(read.filterCutoff == state.filterCutoff);
  REQUIRE(read.gateLengthTicks == state.gateLengthTicks);
  REQUIRE(seq.getCurrentStep() == before);
  const auto selected = seq.getPlaybackStep(0);
  REQUIRE(selected.noteIndex == 1);
  REQUIRE(selected.octaveOffset == 0);
  REQUIRE(seq.getCurrentStepForParameter(ParamId::Octave) == 4);
  seq.resetModifierStep(0);
  REQUIRE_FALSE(seq.getPlaybackStep(0).isGateActive);
  REQUIRE(seq.getPlaybackStep(0).noteIndex == 0);
}

TEST_CASE("Full melody input range reaches distinct scale steps from default base", "[voice_edit][oled]") {
  auto c = VoicePresets::getSquareVoice();
  enablePatch(c);
  for (int index = 0; index <= 36; ++index)
    REQUIRE(composeLane(ParamId::Note, float(index), &c) == index);
  c.baseNote = 7;
  REQUIRE(composeLane(ParamId::Note, 2, &c) == 9);
  REQUIRE(composeLane(ParamId::Note, 36, &c) == 36);
}

TEST_CASE("Displayed notes use the same tuning as rendered oscillator pitches", "[voice_edit][oled]") {
  auto c = VoicePresets::getSquareVoice();
  enablePatch(c);
  c.harmony[0] = 0;
  c.oscDetuning[0] = 0;
  c.highPassFreq = 0;
  Voice voice(0, c);
  uint8_t selectedScale = 0;
  voice.setScaleTable(scale, SCALES_COUNT);
  voice.setCurrentScalePointer(&selectedScale);
  voice.init(48000);
  for (selectedScale = 0; selectedScale < SCALES_COUNT; ++selectedScale) {
    for (int n = 0; n <= 36; ++n) {
      VoiceState state;
      state.noteIndex = float(n);
      state.octaveOffset = 12;
      state.isGateHigh = true;
      voice.updateParameters(state);
      voice.process();
      const int midi = MusicalValues::midiNote(float(n), 12, 0, scale[selectedScale]);
      REQUIRE(voice.getCachedFrequency(0) == Approx(440.0f * std::pow(2.0f, (midi - 69) / 12.0f)).epsilon(0.0001));
    }
  }
  char text[48];
  Step step;
  step.noteIndex = 1;
  MusicalValues::noteName(step.noteIndex, 0, scale[0], text, sizeof(text));
  REQUIRE(std::string(text) == "D5");
  MusicalValues::noteName(step.noteIndex, 0, scale[2], text, sizeof(text));
  REQUIRE(std::string(text) == "C#5");
  c = VoicePresets::getBassVoice();
  step.noteIndex = 0;
  MusicalValues::format(ParamId::Note, step, c, scale[0], 90, text, sizeof(text));
  REQUIRE(std::string(text) == "C4/C5");
}

TEST_CASE("Sequencer OLED formats final physical and preset-specific units", "[voice_edit][oled]") {
  auto c = VoicePresets::getSquareVoice();
  enablePatch(c);
  Step step = MusicalValues::baseStep(c);
  char text[48];
  const auto formatted = [&](ParamId id) {
    MusicalValues::format(id, step, c, scale[0], 120, text, sizeof(text));
    return std::string(text);
  };
  REQUIRE(formatted(ParamId::Note) == "C5");
  REQUIRE(formatted(ParamId::Attack) == "20.0ms");
  REQUIRE(formatted(ParamId::Decay) == "400.0ms");
  REQUIRE(formatted(ParamId::Velocity) == "0.50x");
  REQUIRE(formatted(ParamId::GateLength) == "62.5ms");
  REQUIRE(formatted(ParamId::Octave) == "+0 oct");
  REQUIRE(formatted(ParamId::Slide) == "Off");
  REQUIRE(formatted(ParamId::Gate) == "On");
  REQUIRE(formatted(ParamId::Filter).find("Hz") != std::string::npos);
  c.hasFilter = false;
  REQUIRE(formatted(ParamId::Filter) == "Bypass");
  c.hasEnvelope = false;
  REQUIRE(formatted(ParamId::Attack) == "Off");
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    c = VoicePresets::getPresetConfig(preset);
    enablePatch(c);
    step = MusicalValues::baseStep(c);
    INFO(VoicePresets::getPresetName(preset));
    REQUIRE(step.gateLengthTicks == 90);
    REQUIRE(step.octaveOffset == 0);
    for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane) {
      const auto id = static_cast<ParamId>(lane);
      const std::string value = formatted(id);
      REQUIRE_FALSE(value.empty());
      REQUIRE(value.find("mod") == std::string::npos);
      if (id == ParamId::Note || id == ParamId::Octave || id == ParamId::GateLength)
        REQUIRE(value.find('%') == std::string::npos);
      const auto &binding = VoiceParameters::binding(c, id);
      if (binding.unit == VoiceParameterUnit::Ratio) REQUIRE(value.back() == 'x');
    }
  }
}

TEST_CASE("Patch randomization stays within its depth around the preset bases", "[voice_edit][recording]") {
  auto c = VoicePresets::getDigitalVoice();
  enablePatch(c);
  Sequencer seq;
  seedModifiers(seq);
  seq.setPlaybackTransform(composeLane, &c, mapOctave);
  for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
    seq.setParameterStepCount(static_cast<ParamId>(lane), 64);
  // Depth D moves a step at most D% of the way from its base to a lane end.
  const auto reach = [&](ParamId id, float effective, float depth) {
    const float base = laneBase(id, c);
    CHECK(effective >= base - depth * base - 1e-4f);
    CHECK(effective <= base + depth * (1.0f - base) + 1e-4f);
  };
  for (uint8_t depth : {uint8_t{10}, ParameterManager::kDefaultRandomizeDepth, uint8_t{60}}) {
    for (uint64_t seed : {1ull, 2ull, 3ull, 4ull}) {
      INFO("depth " << int(depth) << " seed " << seed);
      seq.randomizeParameters(depth, seed);
      for (uint8_t i = 0; i < 64; ++i) {
        auto step = seq.getPlaybackStep(i);
        REQUIRE(step.noteIndex >= 0);
        REQUIRE(step.noteIndex <= 12);
        REQUIRE(step.noteIndex == std::round(step.noteIndex));
        REQUIRE(step.octaveOffset == 0);
        REQUIRE(step.gateLengthTicks == 90);
        reach(ParamId::Velocity, step.velocityLevel, depth / 100.0f);
        reach(ParamId::Filter, step.filterCutoff, depth / 100.0f);
        reach(ParamId::Attack, step.attackTimeSeconds, depth / 100.0f);
        reach(ParamId::Decay, step.decayTimeSeconds, depth / 100.0f);
        if (depth == ParameterManager::kDefaultRandomizeDepth) {
          // Digital's 15 ms attack stays a playable step attack (~6..83 ms).
          REQUIRE(MusicalValues::attackSeconds(step.attackTimeSeconds) >= c.defaultAttack / 4.0f);
          REQUIRE(MusicalValues::attackSeconds(step.attackTimeSeconds) <= c.defaultAttack * 6.0f);
        }
      }
    }
  }
}

TEST_CASE("Steps that follow the patch play each preset's own envelope",
          "[voice_edit][envelope]") {
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    auto c = VoicePresets::getPresetConfig(preset);
    enablePatch(c);
    if (!VoiceParameters::layout(c).envelopeFromTracks ||
        VoiceParameters::binding(c, ParamId::Attack).target)
      continue;
    INFO(VoicePresets::getPresetName(preset));
    constexpr float kPatch = SequencerConstants::LANE_FOLLOWS_PATCH;
    REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, kPatch, &c)) ==
            Approx(c.defaultAttack).epsilon(1e-3));
    REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, kPatch, &c)) ==
            Approx(c.defaultDecay).epsilon(1e-3));
    REQUIRE(composeLane(ParamId::Sustain, kPatch, &c) == Approx(c.defaultSustain));
    REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Release, kPatch, &c)) ==
            Approx(c.defaultRelease).epsilon(1e-3));
    REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, 1.0f, &c)) == Approx(2.0f));
    REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 1.0f, &c)) == Approx(10.0f));
  }
}

TEST_CASE("Attack base edits stay inside the attack lane", "[voice_edit][encoder]") {
  auto c = VoicePresets::getDigitalVoice();
  enablePatch(c);
  setValue(Id::Attack, c, 5.0f);
  REQUIRE(c.defaultAttack == Approx(2.0f));
  c.defaultAttack = 0.01f;
  // The envelope page edits the same base while the lane drives the envelope.
  setValue(Id::EnvAttack, c, 5.0f);
  REQUIRE(c.defaultAttack == Approx(2.0f));
  c.defaultAttack = 1.0f;
  adjust(Id::EnvAttack, c, 1.0f);
  REQUIRE(c.defaultAttack == Approx(2.0f));
  char text[24];
  format(Id::EnvAttack, c, text, sizeof(text));
  REQUIRE(std::string(text) == "2.000 s");

  // Envelopes that ignore the lanes keep their long attack range.
  auto pad = VoicePresets::getPresetConfigByName("SilkPad");
  enablePatch(pad);
  REQUIRE_FALSE(VoiceParameters::layout(pad).envelopeFromTracks);
  setValue(Id::EnvAttack, pad, 5.0f);
  REQUIRE(pad.defaultAttack == Approx(5.0f));
}

TEST_CASE("Stepped values move one step per adjust whatever the delta",
          "[voice_edit][encoder]") {
  for (Id id : {Id::Note, Id::Octave, Id::Engine, Id::Recipe, Id::OscCount,
                Id::Wave1, Id::Harmony1, Id::Harmony2, Id::Harmony3,
                Id::EnvelopeOn, Id::Gate, Id::Slide})
    CHECK(stepped(id));
  for (Id id : {Id::Velocity, Id::Cutoff, Id::Attack, Id::Decay,
                Id::GateLength, Id::Resonance, Id::SlideTime, Id::Level1})
    CHECK_FALSE(stepped(id));

  auto config = VoicePresets::getDigitalVoice();
  enablePatch(config);
  config.baseNote = 10;
  adjust(Id::Note, config, 0.0001f);
  REQUIRE(config.baseNote == 11);
  adjust(Id::Note, config, -0.9f);
  REQUIRE(config.baseNote == 10);
  config.baseOctave = 0;
  adjust(Id::Octave, config, 0.5f);
  REQUIRE(config.baseOctave == 12);
}

TEST_CASE("An absent hand leaves recorded modifiers untouched",
          "[voice_edit][recording]") {
  // Playback passes a negative distance while no hand is in range; the step
  // must keep its recording rather than take a minimum-distance value.
  auto config = VoicePresets::getDigitalVoice();
  enablePatch(config);
  Sequencer seq;
  seedModifiers(seq);
  seq.setPlaybackTransform(composeLane, &config, mapOctave);
  seq.setStepParameterValue(ParamId::Gate, 0, 1);
  seq.setStepParameterValue(ParamId::Filter, 0, 0.8f);
  seq.start();
  seq.setRecordingInput(0.0f);
  VoiceState state;
  seq.advanceStep(0, -1, false, false, true, false, false, false, -1, &state);
  REQUIRE(seq.getStepParameterValue(ParamId::Filter, 0) == Approx(0.8f));
}
namespace {
// RMS of one gated step while `edit` runs every millisecond, as the lidar,
// faders and encoder did through updateActiveVoiceState().
double rmsWhileEditing(uint8_t preset, void (*edit)(Sequencer &, VoiceState &)) {
  VoiceManager manager(1);
  manager.init(48000.0f);
  VoiceConfig config = VoicePresets::getPresetConfig(preset);
  enablePatch(config);
  const uint8_t id = manager.addVoice(config);
  Sequencer seq;
  seq.setPlaybackTransform(composeLane, manager.getVoiceConfig(id), mapOctave);
  seedModifiers(seq);
  seq.setStepParameterValue(ParamId::Gate, 0, 1);
  seq.setStepParameterValue(ParamId::GateLength, 0, 1.0f);
  seq.start();
  manager.setTransportMuted(false);
  // Let the staged patch apply (it waits for a low gate) before the step.
  for (int i = 0; i < 480; ++i)
    manager.processAllVoices();
  VoiceState state;
  seq.advanceStep(0, -1, false, false, false, false, false, false, -1, &state);
  manager.updateVoiceState(id, state);
  state.shouldRetrigger = false; // the event belongs to the push above
  double sum = 0;
  constexpr int kSamples = 4800;
  for (int i = 0; i < kSamples; ++i) {
    if (i % 48 == 47) {
      edit(seq, state);
      manager.updateVoiceState(id, state);
    }
    const double y = manager.processAllVoices();
    sum += y * y;
  }
  return std::sqrt(sum / kSamples);
}
} // namespace

TEST_CASE("Live step edits keep oscillator voices sounding",
          "[voice_edit][recording]") {
  // Re-running the step for each edit retriggered the envelope every
  // millisecond: oscillator voices fell near silent while waveguides, which
  // re-pluck, stayed loud. Refreshing in place must sound like no edit.
  const uint8_t square = static_cast<uint8_t>(VoicePresets::findPreset("Square"));
  const double untouched = rmsWhileEditing(square, [](Sequencer &, VoiceState &) {});
  const double refreshed = rmsWhileEditing(square, [](Sequencer &s, VoiceState &v) {
    s.refreshVoiceParameters(&v);
  });
  const double retriggered = rmsWhileEditing(square, [](Sequencer &s, VoiceState &v) {
    s.previewActiveStep(&v);
  });
  REQUIRE(untouched > 0.01);
  CHECK(refreshed == Approx(untouched).epsilon(0.05));
  CHECK(retriggered < untouched * 0.5);
}

TEST_CASE("RubberSub sequences full range without dead zones on Attack, Cutoff, Decay",
          "[voice_edit][rubbersub]") {
  VoiceConfig rubberSub = VoicePresets::getRubberSubVoice();
  enablePatch(rubberSub);

  // Attack: 2 ms base (normalized ~0.09) spans 1 ms to 2 s across hand range
  const float attackBase = laneBase(ParamId::Attack, rubberSub);
  REQUIRE(attackBase == Approx(attackNormalize(0.002f)));
  REQUIRE(composeLane(ParamId::Attack, 0.0f, &rubberSub) == 0.0f);
  REQUIRE(composeLane(ParamId::Attack, 0.1f, &rubberSub) > 0.0f); // Zero dead zone!
  REQUIRE(composeLane(ParamId::Attack, 0.25f, &rubberSub) > 0.0f);
  constexpr float kPatch = SequencerConstants::LANE_FOLLOWS_PATCH;
  REQUIRE(composeLane(ParamId::Attack, kPatch, &rubberSub) == Approx(attackBase));
  REQUIRE(composeLane(ParamId::Attack, 0.75f, &rubberSub) > attackBase);
  REQUIRE(composeLane(ParamId::Attack, 1.0f, &rubberSub) == 1.0f);

  REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, 0.0f, &rubberSub)) == Approx(0.001f));
  REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, kPatch, &rubberSub)) == Approx(0.002f));
  REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, 1.0f, &rubberSub)) == Approx(2.0f));

  // Cutoff: 0.5 base (the 320 Hz center) spans 90 Hz to 1200 Hz across hand range
  const float filterBase = laneBase(ParamId::Filter, rubberSub);
  REQUIRE(filterBase == Approx(0.5f));
  const auto &cutoff = VoiceParameters::layout(rubberSub);
  REQUIRE(VoiceParameters::mapCutoff(cutoff, composeLane(ParamId::Filter, 0.0f, &rubberSub)) == Approx(90.0f));
  REQUIRE(VoiceParameters::mapCutoff(cutoff, composeLane(ParamId::Filter, 0.5f, &rubberSub)) == Approx(320.0f));
  REQUIRE(VoiceParameters::mapCutoff(cutoff, composeLane(ParamId::Filter, 1.0f, &rubberSub)) == Approx(1200.0f));
  REQUIRE(composeLane(ParamId::Filter, 0.0f, &rubberSub) == 0.0f);
  REQUIRE(composeLane(ParamId::Filter, 0.1f, &rubberSub) > 0.0f); // Zero dead zone!
  REQUIRE(composeLane(ParamId::Filter, kPatch, &rubberSub) == Approx(filterBase));
  REQUIRE(composeLane(ParamId::Filter, 1.0f, &rubberSub) == 1.0f);

  // Decay: 160 ms base spans 1 ms to 10 s across hand range
  const float decayBase = laneBase(ParamId::Decay, rubberSub);
  REQUIRE(decayBase == Approx(timeNormalize(0.16f)));
  REQUIRE(composeLane(ParamId::Decay, 0.0f, &rubberSub) == 0.0f);
  REQUIRE(composeLane(ParamId::Decay, kPatch, &rubberSub) == Approx(decayBase));
  REQUIRE(composeLane(ParamId::Decay, 1.0f, &rubberSub) == 1.0f);
  REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 0.0f, &rubberSub)) == Approx(0.001f));
  REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, kPatch, &rubberSub)) == Approx(0.16f));
  REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 1.0f, &rubberSub)) == Approx(10.0f));
}

TEST_CASE("All factory presets sequence full continuous range without dead zones",
          "[voice_edit][presets]") {
  for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
    VoiceConfig c = VoicePresets::getPresetConfig(p);
    enablePatch(c);
    INFO("Testing preset: " << VoicePresets::getPresetName(p));

    // Absolute lanes span their whole range whatever the patch value.
    for (ParamId lane : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay,
                         ParamId::Sustain, ParamId::Release}) {
      for (float value : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        REQUIRE(composeLane(lane, value, &c) == Approx(value));
      REQUIRE(composeLane(lane, SequencerConstants::LANE_FOLLOWS_PATCH, &c) == Approx(laneBase(lane, c)));
    }
    // Octave stays an offset around the patch octave.
    const ParamId lane = ParamId::Octave;
    const float base = laneBase(lane, c);
    REQUIRE(composeLane(lane, 0.0f, &c) == 0.0f);
    REQUIRE(composeLane(lane, 0.5f, &c) == Approx(base));
    REQUIRE(composeLane(lane, 1.0f, &c) == 1.0f);
  }
}

TEST_CASE("OLED display formatting never outputs ratio fallback (.xx) on Attack, Cutoff, or Decay",
          "[voice_edit][oled]") {
  for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
    VoiceConfig c = VoicePresets::getPresetConfig(p);
    enablePatch(c);
    INFO("Testing OLED format for preset: " << VoicePresets::getPresetName(p));

    Step step{};
    for (float val : {0.0f, 0.01f, 0.1f, 0.37f, 0.5f, 0.75f, 1.0f}) {
      step.attackTimeSeconds = val;
      step.filterCutoff = val;
      step.decayTimeSeconds = val;
      step.velocityLevel = val;

      char buf[48];
      // Attack must format as time (or custom unit), never %.2fx fallback
      MusicalValues::format(ParamId::Attack, step, c, nullptr, 120.0f, buf, sizeof(buf));
      std::string atk(buf);
      REQUIRE(atk != "0.01x");
      if (VoicePresets::getPresetName(p) == std::string("RubberSub")) {
        REQUIRE((atk.find("ms") != std::string::npos || atk.find("s") != std::string::npos));
      }

      // Cutoff must format as Hz (or custom unit), never %.2fx fallback
      MusicalValues::format(ParamId::Filter, step, c, nullptr, 120.0f, buf, sizeof(buf));
      std::string flt(buf);
      REQUIRE(flt != "0.01x");
      if (VoicePresets::getPresetName(p) == std::string("RubberSub")) {
        REQUIRE(flt.find("Hz") != std::string::npos);
      }

      // Decay must format as time (or custom unit), never %.2fx fallback
      MusicalValues::format(ParamId::Decay, step, c, nullptr, 120.0f, buf, sizeof(buf));
      std::string dec(buf);
      REQUIRE(dec != "0.01x");
      if (VoicePresets::getPresetName(p) == std::string("RubberSub")) {
        REQUIRE((dec.find("ms") != std::string::npos || dec.find("s") != std::string::npos));
      }
    }
  }
}

TEST_CASE("RubberSub audio synthesis produces fat audible sub-bass and distinct filter response",
          "[voice_edit][rubbersub][audio]") {
  const uint8_t rubberSub = static_cast<uint8_t>(VoicePresets::findPreset("RubberSub"));
  const double untouchedRms = rmsWhileEditing(rubberSub, [](Sequencer &, VoiceState &) {});
  REQUIRE(untouchedRms > 0.02); // Robust, audible audio output

  // Verify cutoff sweep produces audible timbre change
  const double openCutoffRms = rmsWhileEditing(rubberSub, [](Sequencer &s, VoiceState &v) {
    v.filterCutoff = 1.0f;
    s.refreshVoiceParameters(&v);
  });
  REQUIRE(openCutoffRms > 0.02);
}

TEST_CASE("SubFunk audio synthesis produces audible sub-bass and distinct filter response",
          "[voice_edit][subfunk][audio]") {
  const uint8_t subFunk = static_cast<uint8_t>(VoicePresets::findPreset("SubFunk"));
  const auto config = VoicePresets::getSubFunkVoice();
  REQUIRE(config.highPassFreq == 25.0f);
  REQUIRE(config.filterEnvelopeRest == 0.35f);
  REQUIRE(config.oscWaveforms[1] == WAVE_BSP_SQUARE);
  REQUIRE(config.oscAmplitudes[1] == Approx(0.35f));
  REQUIRE(config.filterRes == Approx(0.6f));
  REQUIRE(config.overdriveDrive == Approx(0.45f));

  const double untouchedRms = rmsWhileEditing(subFunk, [](Sequencer &, VoiceState &) {});
  REQUIRE(untouchedRms > 0.05); // Robust, audible output

  const double lowCutoffRms = rmsWhileEditing(subFunk, [](Sequencer &s, VoiceState &v) {
    s.setStepParameterValue(ParamId::Filter, 0, 0.0f);
    s.refreshVoiceParameters(&v);
  });
  REQUIRE(lowCutoffRms > 0.05);

  const double openCutoffRms = rmsWhileEditing(subFunk, [](Sequencer &s, VoiceState &v) {
    s.setStepParameterValue(ParamId::Filter, 0, 1.0f);
    s.refreshVoiceParameters(&v);
  });
  REQUIRE(openCutoffRms > 0.05);
}

TEST_CASE("Live parameter modulation with distance sensor produces distinct values across all lanes",
          "[voice_edit][live_modulation]") {
  auto config = VoicePresets::getSubFunkVoice();
  enablePatch(config);

  // Helper simulating live composed step calculation as done on the OLED
  auto composeLive = [&](ParamId id, float norm) -> Step {
    Step s{};
    const float stored = mapNormalizedValueToParamRange(id, norm);
    const float composed = VoiceEdit::composeLane(id, stored, &config);
    switch (id) {
      case ParamId::Velocity: s.velocityLevel = composed; break;
      case ParamId::Filter: s.filterCutoff = composed; break;
      case ParamId::Attack: s.attackTimeSeconds = composed; break;
      case ParamId::Decay: s.decayTimeSeconds = composed; break;
      case ParamId::Note: s.noteIndex = composed; break;
      case ParamId::Octave: s.octaveOffset = VoiceEdit::mapOctave(composed); break;
      case ParamId::GateLength:
        s.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f,
            composed * SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
        break;
      default: break;
    }
    return s;
  };

  // Filter cutoff: min, mid, max format distinct frequencies
  char lowBuf[32], midBuf[32], highBuf[32];
  MusicalValues::format(ParamId::Filter, composeLive(ParamId::Filter, 0.0f), config, nullptr, 120.0f, lowBuf, sizeof(lowBuf));
  MusicalValues::format(ParamId::Filter, composeLive(ParamId::Filter, 0.5f), config, nullptr, 120.0f, midBuf, sizeof(midBuf));
  MusicalValues::format(ParamId::Filter, composeLive(ParamId::Filter, 1.0f), config, nullptr, 120.0f, highBuf, sizeof(highBuf));
  CHECK(std::string(lowBuf) == "60Hz");
  CHECK(std::string(midBuf) == "420Hz");
  CHECK(std::string(highBuf) == "1600Hz");

  // Attack: min, mid, max format distinct times
  MusicalValues::format(ParamId::Attack, composeLive(ParamId::Attack, 0.0f), config, nullptr, 120.0f, lowBuf, sizeof(lowBuf));
  MusicalValues::format(ParamId::Attack, composeLive(ParamId::Attack, 0.5f), config, nullptr, 120.0f, midBuf, sizeof(midBuf));
  MusicalValues::format(ParamId::Attack, composeLive(ParamId::Attack, 1.0f), config, nullptr, 120.0f, highBuf, sizeof(highBuf));
  CHECK(std::string(lowBuf) == "1.0ms");
  CHECK(std::string(midBuf) == "44.7ms"); // absolute lane center, 1 ms..2 s log
  CHECK(std::string(highBuf) == "2.00s");

  // Decay: min, mid, max format distinct times
  MusicalValues::format(ParamId::Decay, composeLive(ParamId::Decay, 0.0f), config, nullptr, 120.0f, lowBuf, sizeof(lowBuf));
  MusicalValues::format(ParamId::Decay, composeLive(ParamId::Decay, 0.5f), config, nullptr, 120.0f, midBuf, sizeof(midBuf));
  MusicalValues::format(ParamId::Decay, composeLive(ParamId::Decay, 1.0f), config, nullptr, 120.0f, highBuf, sizeof(highBuf));
  CHECK(std::string(lowBuf) == "1.0ms");
  CHECK(std::string(midBuf) == "100.0ms"); // absolute lane center, 1 ms..10 s log
  CHECK(std::string(highBuf) == "10.00s");

  // Velocity: min, mid, max format distinct multipliers
  MusicalValues::format(ParamId::Velocity, composeLive(ParamId::Velocity, 0.0f), config, nullptr, 120.0f, lowBuf, sizeof(lowBuf));
  MusicalValues::format(ParamId::Velocity, composeLive(ParamId::Velocity, 0.5f), config, nullptr, 120.0f, midBuf, sizeof(midBuf));
  MusicalValues::format(ParamId::Velocity, composeLive(ParamId::Velocity, 1.0f), config, nullptr, 120.0f, highBuf, sizeof(highBuf));
  CHECK(std::string(lowBuf) != std::string(midBuf));
  CHECK(std::string(midBuf) != std::string(highBuf));

  // Octave: min, mid, max format distinct octaves
  MusicalValues::format(ParamId::Octave, composeLive(ParamId::Octave, 0.0f), config, nullptr, 120.0f, lowBuf, sizeof(lowBuf));
  MusicalValues::format(ParamId::Octave, composeLive(ParamId::Octave, 0.5f), config, nullptr, 120.0f, midBuf, sizeof(midBuf));
  MusicalValues::format(ParamId::Octave, composeLive(ParamId::Octave, 1.0f), config, nullptr, 120.0f, highBuf, sizeof(highBuf));
  CHECK(std::string(lowBuf) == "-2 oct");
  CHECK(std::string(midBuf) == "+0 oct");
  CHECK(std::string(highBuf) == "+2 oct");
}




TEST_CASE("Absolute lanes play their own value or the patch value", "[voice_edit][absolute]") {
  constexpr ParamId kAbsolute[] = {ParamId::Velocity, ParamId::Filter, ParamId::Attack,
                                   ParamId::Decay, ParamId::Sustain, ParamId::Release};
  for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
    VoiceConfig c = VoicePresets::getPresetConfig(p);
    enablePatch(c);
    INFO(VoicePresets::getPresetName(p));
    for (ParamId lane : kAbsolute) {
      INFO(static_cast<int>(lane));
      CHECK(composeLane(lane, SequencerConstants::LANE_FOLLOWS_PATCH, &c) ==
            Approx(laneBase(lane, c)));
      for (float value : {0.0f, 0.2f, 0.5f, 0.85f, 1.0f})
        CHECK(composeLane(lane, value, &c) == Approx(value));
    }
  }
}

TEST_CASE("A patch value at the end of its range no longer swallows step edits",
          "[voice_edit][absolute]") {
  // Offsets used to scale by the base below the lane center: with a base of
  // 0 every lower-half hand height or encoder turn composed to exactly 0.
  VoiceConfig c = VoicePresets::getSquareVoice();
  enablePatch(c);
  c.filterCutoffBase = 0.0f;
  c.defaultDecay = 0.001f;
  c.baseVelocity = 1.0f;
  for (ParamId lane : {ParamId::Filter, ParamId::Decay, ParamId::Velocity}) {
    INFO(static_cast<int>(lane));
    CHECK(composeLane(lane, 0.2f, &c) == Approx(0.2f));
    CHECK(composeLane(lane, 0.4f, &c) == Approx(0.4f));
  }
}

TEST_CASE("Sustain and Release bases follow the patch envelope", "[voice_edit][absolute]") {
  VoiceConfig c = VoicePresets::getSquareVoice();
  enablePatch(c);
  CHECK(laneBase(ParamId::Sustain, c) == Approx(c.defaultSustain));
  CHECK(MusicalValues::envelopeSeconds(laneBase(ParamId::Release, c)) ==
        Approx(c.defaultRelease).epsilon(1e-3));
  // Strings have no envelope: their Sustain/Release lanes shape the pluck.
  VoiceConfig wg = VoicePresets::getPresetConfig(static_cast<uint8_t>(VoicePresets::findPreset("WgPluck")));
  enablePatch(wg);
  CHECK(VoiceParameters::binding(wg, ParamId::Sustain).target == &VoiceConfig::wgPickPosition);
  CHECK(VoiceParameters::binding(wg, ParamId::Release).target == &VoiceConfig::wgStiffness);
  CHECK(std::string(laneName(ParamId::Sustain, wg)) == "Position");
  CHECK(std::string(laneName(ParamId::Release, wg)) == "Stiffness");
  CHECK(std::string(laneName(ParamId::Sustain, c)) == "Sustain");
  CHECK(sequenceLane(Id::PickPosition, wg) == ParamId::Sustain);
  CHECK(sequenceLane(Id::Sustain, c) == ParamId::Sustain);
  CHECK(sequenceLane(Id::Sustain, wg) == ParamId::Count);
}

TEST_CASE("Old offset lanes convert to what they played", "[voice_edit][absolute]") {
  VoiceConfig c = VoicePresets::getSquareVoice();
  enablePatch(c);
  // A format-1 lane: offsets around the patch, 0.5 = the patch value.
  float decay[SequencerConstants::MAX_STEPS_COUNT];
  for (float &value : decay)
    value = 0.5f;
  decay[2] = 0.25f;
  decay[40] = 1.0f;
  const float base = laneBase(ParamId::Decay, c);
  convertOffsetValues(ParamId::Decay, decay, SequencerConstants::MAX_STEPS_COUNT, c);
  CHECK(followsPatch(decay[0]));
  CHECK(followsPatch(decay[63]));
  CHECK(decay[2] == Approx(base * 0.5f));
  CHECK(decay[40] == Approx(1.0f));
  // Played values are unchanged by the conversion.
  CHECK(composeLane(ParamId::Decay, decay[0], &c) == Approx(base));
  CHECK(composeLane(ParamId::Decay, decay[2], &c) == Approx(base * 0.5f));
  // Offset lanes (Note, Octave, GateLength) are not converted.
  float note[4] = {0.5f, 3.0f, 0.0f, 12.0f};
  convertOffsetValues(ParamId::Note, note, 4, c);
  CHECK(note[0] == 0.5f);
  CHECK(note[1] == 3.0f);
}

namespace {
// One voice driven the way the firmware drives it: the sequencer composes
// lanes against the voice's requested config, and every publish goes
// through VoiceManager like publishVoiceState() on Core 0.
struct LiveVoice {
  VoiceManager manager{1};
  Sequencer seq;
  VoiceState state; // the retained VoiceSystem copy
  uint8_t id = 0;
  explicit LiveVoice(uint8_t preset) {
    manager.init(48000.0f);
    VoiceConfig config = VoicePresets::getPresetConfig(preset);
    enablePatch(config);
    id = manager.addVoice(config);
    seq.setPlaybackTransform(composeLane, manager.getVoiceConfig(id), mapOctave);
    seedModifiers(seq);
    for (uint8_t s = 0; s < 16; ++s) {
      seq.setStepParameterValue(ParamId::Gate, s, 1);
      seq.setStepParameterValue(ParamId::GateLength, s, 1);
    }
    seq.start();
    manager.setTransportMuted(false);
    render(480); // the staged patch applies while the gate is low
  }
  const VoiceConfig &config() { return *manager.getVoiceConfig(id); }
  void publish(const VoiceState &s) {
    manager.updateVoiceState(id, s);
    state = s;
    state.shouldRetrigger = false;
  }
  void step(uint32_t clockStep) {
    VoiceState next = state;
    seq.advanceStep(clockStep, -1, false, false, false, false, false, false, -1, &next);
    publish(next);
  }
  // recordParameter() while playing: the lane's playing step, then an
  // in-place refresh of the sounding note.
  void recordLive(ParamId lane, float position) {
    if (seq.recordLiveValue(lane, mapNormalizedValueToParamRange(lane, position))) {
      seq.refreshVoiceParameters(&state);
      publish(state);
    }
  }
  // VoiceEditor::encoder(): a new base, then the same in-place refresh.
  void editBase(Id base, float value) {
    VoiceConfig next = config();
    setValue(base, next, value);
    manager.setVoiceConfig(id, next);
    seq.refreshVoiceParameters(&state);
    publish(state);
  }
  // ENV mode: a fader writes the selected step's absolute value.
  void editStep(ParamId lane, uint8_t stepIndex, float position) {
    seq.editStepValue(lane, stepIndex, mapNormalizedValueToParamRange(lane, position));
  }
  std::string oled(const Step &values, ParamId lane) {
    char text[48];
    MusicalValues::format(lane, values, config(), nullptr, 90.0f, text, sizeof(text));
    return text;
  }
  std::vector<float> render(int samples) {
    std::vector<float> out(samples);
    for (float &y : out)
      y = manager.processAllVoices();
    return out;
  }
  // Just past the preset's attack, while the note is loud.
  int attackSamples() { return static_cast<int>(config().defaultAttack * 48000.0f) + 480; }
};

double rms(const std::vector<float> &x) {
  double energy = 0;
  for (float y : x)
    energy += double(y) * y;
  return std::sqrt(energy / x.size());
}
// 0 for identical renders, about 1 for unrelated ones.
double difference(const std::vector<float> &a, const std::vector<float> &b) {
  double d = 0, energy = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    d += double(a[i] - b[i]) * (a[i] - b[i]);
    energy += double(a[i]) * a[i] + double(b[i]) * b[i];
  }
  return energy > 0 ? d / energy : 0;
}
std::vector<uint8_t> oscillatorPresets() {
  std::vector<uint8_t> presets;
  for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p)
    if (VoicePresets::getPresetConfig(p).engine == ENGINE_OSC)
      presets.push_back(p);
  return presets;
}
bool lowPass(const VoiceConfig &c) {
  return c.filterMode == VoiceFilterMode::LP24 || c.filterMode == VoiceFilterMode::LP12;
}
} // namespace

TEST_CASE("Held-button recording between steps moves an oscillator voice's cutoff",
          "[voice_edit][recording][live]") {
  REQUIRE(oscillatorPresets().size() == 9);
  for (uint8_t preset : oscillatorPresets()) {
    INFO(VoicePresets::getPresetName(preset));
    LiveVoice dark(preset), bright(preset);
    dark.step(0);
    bright.step(0);
    dark.render(dark.attackSamples());
    bright.render(bright.attackSamples());
    // Mid-step, as the lidar moves: the same note keeps sounding, re-filtered.
    dark.recordLive(ParamId::Filter, 0.0f);
    bright.recordLive(ParamId::Filter, 1.0f);
    CHECK(dark.seq.getStepParameterValue(ParamId::Filter, 0) == 0.0f);
    CHECK(bright.seq.getStepParameterValue(ParamId::Filter, 0) == 1.0f);
    CHECK(dark.oled(dark.seq.getPlaybackStep(), ParamId::Filter) !=
          bright.oled(bright.seq.getPlaybackStep(), ParamId::Filter));
    const auto low = dark.render(4800), high = bright.render(4800);
    CHECK(difference(low, high) > 0.1);
    if (lowPass(dark.config()))
      CHECK(rms(high) > 1.5 * rms(low));
  }
}

TEST_CASE("Recorded attack and decay are heard on every oscillator voice",
          "[voice_edit][recording][live]") {
  for (uint8_t preset : oscillatorPresets()) {
    INFO(VoicePresets::getPresetName(preset));
    LiveVoice fast(preset), slow(preset);
    fast.seq.setStepParameterValue(ParamId::Attack, 0, 0.0f);
    slow.seq.setStepParameterValue(ParamId::Attack, 0, 1.0f);
    fast.step(0);
    slow.step(0);
    CHECK(fast.oled(fast.seq.getPlaybackStep(), ParamId::Attack) == "1.0ms");
    CHECK(slow.oled(slow.seq.getPlaybackStep(), ParamId::Attack) == "2.00s");
    CHECK(rms(slow.render(960)) < 0.1 * rms(fast.render(960)));

    LiveVoice shortDecay(preset), longDecay(preset);
    shortDecay.seq.setStepParameterValue(ParamId::Decay, 0, 0.0f);
    longDecay.seq.setStepParameterValue(ParamId::Decay, 0, 1.0f);
    shortDecay.step(0);
    longDecay.step(0);
    CHECK(shortDecay.oled(shortDecay.seq.getPlaybackStep(), ParamId::Decay) == "1.0ms");
    CHECK(longDecay.oled(longDecay.seq.getPlaybackStep(), ParamId::Decay) == "10.00s");
    shortDecay.render(shortDecay.attackSamples() + 2400);
    longDecay.render(longDecay.attackSamples() + 2400);
    const auto shortTail = shortDecay.render(9600), longTail = longDecay.render(9600);
    CHECK(rms(longTail) > 1.1 * rms(shortTail));
    CHECK(difference(shortTail, longTail) > 0.02);
  }
}

TEST_CASE("An encoder base edit moves an oscillator voice's patch value while it plays",
          "[voice_edit][base][live]") {
  for (uint8_t preset : oscillatorPresets()) {
    INFO(VoicePresets::getPresetName(preset));
    LiveVoice dark(preset), bright(preset);
    dark.step(0);
    bright.step(0);
    dark.render(dark.attackSamples());
    bright.render(bright.attackSamples());
    dark.editBase(Id::Cutoff, 0.0f);
    bright.editBase(Id::Cutoff, 1.0f);
    // The step still follows the patch: only the patch value moved.
    CHECK(followsPatch(dark.seq.getStepParameterValue(ParamId::Filter, 0)));
    CHECK(dark.config().filterCutoffBase == 0.0f);
    const auto &layout = VoiceParameters::layout(dark.config());
    char low[24], high[24];
    std::snprintf(low, sizeof(low), "%.0fHz", layout.cutoffMinimum);
    std::snprintf(high, sizeof(high), "%.0fHz", layout.cutoffMaximum);
    // The OLED base view and the playing step both show the new cutoff.
    CHECK(dark.oled(MusicalValues::baseStep(dark.config()), ParamId::Filter) == low);
    CHECK(bright.oled(MusicalValues::baseStep(bright.config()), ParamId::Filter) == high);
    CHECK(dark.oled(dark.seq.getPlaybackStep(), ParamId::Filter) == low);
    const auto closed = dark.render(4800), open = bright.render(4800);
    CHECK(difference(closed, open) > 0.1);
    if (lowPass(dark.config()))
      CHECK(rms(open) > 1.5 * rms(closed));

    LiveVoice fast(preset), slow(preset);
    fast.editBase(Id::Attack, 0.001f);
    slow.editBase(Id::Attack, kAttackMaxSeconds);
    CHECK(slow.config().defaultAttack == Approx(kAttackMaxSeconds));
    fast.step(0);
    slow.step(0);
    CHECK(rms(slow.render(960)) < 0.1 * rms(fast.render(960)));
  }
}

TEST_CASE("Live envelope edits and patch re-sends never step a sounding note",
          "[voice_edit][live]") {
  // The ADSR counts each stage in samples: a new decay length mid-decay used
  // to jump the level (a click per lidar reading), and every patch publish
  // snapped the cutoff smoother to the unmodulated cutoff.
  for (uint8_t preset : oscillatorPresets()) {
    INFO(VoicePresets::getPresetName(preset));
    LiveVoice untouched(preset), edited(preset);
    untouched.step(0);
    edited.step(0);
    untouched.render(untouched.attackSamples());
    edited.render(edited.attackSamples()); // now in the decay stage
    std::vector<float> a, b;
    for (int k = 0; k < 20; ++k) {
      edited.recordLive(ParamId::Decay, k % 2 ? 0.52f : 0.48f); // a steady hand's jitter
      edited.manager.setVoiceConfig(edited.id, edited.config()); // an unchanged encoder publish
      const auto x = untouched.render(480), y = edited.render(480);
      a.insert(a.end(), x.begin(), x.end());
      b.insert(b.end(), y.begin(), y.end());
    }
    CHECK(difference(a, b) < 1e-4); // only the filter coefficient resync

    // The recorded decay still plays: the next time this step sounds. (The
    // cutoff tracks the envelope, so a band-pass can get quieter, not louder.)
    edited.recordLive(ParamId::Decay, 1.0f);
    untouched.step(16);
    edited.step(16);
    untouched.render(untouched.attackSamples() + 2400);
    edited.render(edited.attackSamples() + 2400);
    // Bass sustains at 0.85, so its decay is the subtlest; unedited renders differ by ~1e-6.
    CHECK(difference(untouched.render(9600), edited.render(9600)) > 0.003);
  }
}

namespace {
float peak(const std::vector<float> &x) {
  float p = 0.0f;
  for (float y : x)
    p = std::max(p, std::fabs(y));
  return p;
}
int decaySamples(LiveVoice &v) { return static_cast<int>(v.config().defaultDecay * 48000.0f); }
} // namespace

TEST_CASE("Every preset is audible on a step that follows its patch", "[voice_edit][presets][live]") {
  // Square went silent when its decay lane played ~1 ms: with sustain 0 the
  // note ends at the attack peak. Untouched steps now play the preset's own
  // envelope, whatever a session stored before.
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    INFO(VoicePresets::getPresetName(preset));
    LiveVoice voice(preset);
    voice.step(0);
    CHECK(peak(voice.render(48000)) > 0.02f);
  }
}

TEST_CASE("A step's own sustain holds its note", "[voice_edit][envelope][live]") {
  for (uint8_t preset : oscillatorPresets()) {
    LiveVoice plain(preset), held(preset);
    INFO(VoicePresets::getPresetName(preset));
    held.editStep(ParamId::Sustain, 0, 1.0f);
    plain.step(0);
    held.step(0);
    const int settle = plain.attackSamples() + decaySamples(plain) + 960;
    plain.render(settle);
    held.render(settle);
    const auto a = plain.render(4800), b = held.render(4800);
    CHECK(rms(b) > rms(a));
    CHECK(difference(a, b) > 0.001);
  }
}

TEST_CASE("A step's own release shapes its tail", "[voice_edit][envelope][live]") {
  const uint8_t digital = static_cast<uint8_t>(VoicePresets::findPreset("Digital"));
  LiveVoice brief(digital), ringing(digital);
  brief.editStep(ParamId::Release, 0, 0.0f);    // 1 ms
  ringing.editStep(ParamId::Release, 0, 1.0f);  // 10 s
  brief.step(0);
  ringing.step(0);
  const int settle = brief.attackSamples() + decaySamples(brief) + 960;
  brief.render(settle);
  ringing.render(settle);
  for (LiveVoice *voice : {&brief, &ringing}) {
    VoiceState off = voice->state;
    off.isGateHigh = false;
    voice->publish(off);
  }
  const auto shortTail = brief.render(4800), longTail = ringing.render(4800);
  CHECK(rms(longTail) > 5.0 * rms(shortTail));
}

TEST_CASE("Sustain and release edits wait for the next note instead of stepping it",
          "[voice_edit][envelope][live]") {
  const uint8_t digital = static_cast<uint8_t>(VoicePresets::findPreset("Digital"));
  LiveVoice untouched(digital), edited(digital);
  untouched.step(0);
  edited.step(0);
  const int settle = untouched.attackSamples() + decaySamples(untouched) + 960;
  untouched.render(settle);
  edited.render(settle); // now sustaining
  // ENV mode edits the playing step and refreshes the note in place.
  edited.editStep(ParamId::Sustain, 0, 1.0f);
  edited.editStep(ParamId::Release, 0, 1.0f);
  edited.seq.refreshVoiceParameters(&edited.state);
  edited.publish(edited.state);
  CHECK(difference(untouched.render(4800), edited.render(4800)) < 1e-4);
  // The next note plays the new sustain.
  untouched.step(16);
  edited.step(16);
  untouched.render(settle);
  edited.render(settle);
  CHECK(rms(edited.render(4800)) > 1.2 * rms(untouched.render(4800)));
}

TEST_CASE("A string's ENV lanes 3 and 4 move its pick position and stiffness",
          "[voice_edit][waveguide][live]") {
  const uint8_t pluck = static_cast<uint8_t>(VoicePresets::findPreset("WgPluck"));
  LiveVoice plain(pluck), shaped(pluck);
  shaped.editStep(ParamId::Sustain, 0, 1.0f); // Position: string middle
  shaped.editStep(ParamId::Release, 0, 1.0f); // Stiffness: bell-like
  CHECK(shaped.oled(shaped.seq.getPlaybackStep(0), ParamId::Sustain) == "50%");
  CHECK(shaped.oled(shaped.seq.getPlaybackStep(0), ParamId::Release) == "100%");
  plain.step(0);
  shaped.step(0);
  CHECK(difference(plain.render(9600), shaped.render(9600)) > 0.05);
}
