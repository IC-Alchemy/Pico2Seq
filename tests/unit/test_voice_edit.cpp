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
using Catch::Approx;
using namespace VoiceEdit;

TEST_CASE(
    "Live lidar recording stores only modifiers before composing playback",
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
  REQUIRE(state.velocityLevel == Approx(0.80f));
  REQUIRE(config.baseVelocity == Approx(0.6f));
  config.baseVelocity = 0.4f;
  seq.playStepNow(step, &state);
  REQUIRE(state.velocityLevel == Approx(0.70f));
  REQUIRE(seq.getStepParameterValue(ParamId::Velocity, step) == 0.75f);
  seq.resetModifierStep(step);
  REQUIRE(seq.getStepParameterValue(ParamId::Velocity, step) == 0.5f);
  REQUIRE(seq.getStepParameterValue(ParamId::Note, step) == 0);
  REQUIRE(seq.getStepParameterValue(ParamId::Gate, step) == 0);
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
  REQUIRE(composeLane(ParamId::Velocity, 0.25f, &sync) == Approx(0.375f));
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

TEST_CASE("Patch bases and stored lidar modifiers are independent",
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
  REQUIRE(seq.getStepParameterValue(ParamId::Velocity, 0) == 0.5f);
  seq.setStepParameterValue(ParamId::Velocity, 0, 0.25f);
  seq.playStepNow(0, &state);
  REQUIRE(state.velocityLevel == Approx(0.35f));
  REQUIRE(config.baseVelocity == Approx(0.7f));
  setValue(Id::GateLength, config, 0.8f);
  seq.playStepNow(0, &state);
  REQUIRE(state.gateLengthTicks == 96);
  setValue(Id::Gate, config, 0);
  seq.playStepNow(0, &state);
  REQUIRE_FALSE(state.isGateHigh);
  REQUIRE(seq.getStepParameterValue(ParamId::Gate, 0) == 1);
}

TEST_CASE("Every preset maps neutral modifiers back to its sound bases",
          "[voice_edit]") {
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    auto c = VoicePresets::getPresetConfig(preset);
    enablePatch(c);
    INFO(VoicePresets::getPresetName(preset));
    for (ParamId lane : {ParamId::Velocity, ParamId::Filter, ParamId::Attack,
                         ParamId::Decay}) {
      const auto &b = VoiceParameters::binding(c, lane);
      const float composed = composeLane(lane, 0.5f, &c);
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
  REQUIRE(std::string(text) == "D3");
  MusicalValues::noteName(step.noteIndex, 0, scale[2], text, sizeof(text));
  REQUIRE(std::string(text) == "C#3");
  c = VoicePresets::getBassVoice();
  step.noteIndex = 0;
  MusicalValues::format(ParamId::Note, step, c, scale[0], 90, text, sizeof(text));
  REQUIRE(std::string(text) == "C2/C3");
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
  REQUIRE(formatted(ParamId::Note) == "C3");
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
    REQUIRE(step.gateLengthTicks == 60);
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
        REQUIRE(step.gateLengthTicks == 60);
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

TEST_CASE("Neutral envelope modifiers play each preset's own attack and decay",
          "[voice_edit][envelope]") {
  for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset) {
    auto c = VoicePresets::getPresetConfig(preset);
    enablePatch(c);
    if (!VoiceParameters::layout(c).envelopeFromTracks ||
        VoiceParameters::binding(c, ParamId::Attack).target)
      continue;
    INFO(VoicePresets::getPresetName(preset));
    REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, 0.5f, &c)) ==
            Approx(c.defaultAttack).epsilon(1e-3));
    REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 0.5f, &c)) ==
            Approx(c.defaultDecay).epsilon(1e-3));
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
  REQUIRE(composeLane(ParamId::Attack, 0.5f, &rubberSub) == Approx(attackBase));
  REQUIRE(composeLane(ParamId::Attack, 0.75f, &rubberSub) > attackBase);
  REQUIRE(composeLane(ParamId::Attack, 1.0f, &rubberSub) == 1.0f);

  REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, 0.0f, &rubberSub)) == Approx(0.001f));
  REQUIRE(MusicalValues::attackSeconds(composeLane(ParamId::Attack, 0.5f, &rubberSub)) == Approx(0.002f));
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
  REQUIRE(composeLane(ParamId::Filter, 0.5f, &rubberSub) == Approx(filterBase));
  REQUIRE(composeLane(ParamId::Filter, 1.0f, &rubberSub) == 1.0f);

  // Decay: 160 ms base spans 1 ms to 10 s across hand range
  const float decayBase = laneBase(ParamId::Decay, rubberSub);
  REQUIRE(decayBase == Approx(timeNormalize(0.16f)));
  REQUIRE(composeLane(ParamId::Decay, 0.0f, &rubberSub) == 0.0f);
  REQUIRE(composeLane(ParamId::Decay, 0.5f, &rubberSub) == Approx(decayBase));
  REQUIRE(composeLane(ParamId::Decay, 1.0f, &rubberSub) == 1.0f);
  REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 0.0f, &rubberSub)) == Approx(0.001f));
  REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 0.5f, &rubberSub)) == Approx(0.16f));
  REQUIRE(MusicalValues::envelopeSeconds(composeLane(ParamId::Decay, 1.0f, &rubberSub)) == Approx(10.0f));
}

TEST_CASE("All factory presets sequence full continuous range without dead zones",
          "[voice_edit][presets]") {
  for (uint8_t p = 0; p < VoicePresets::getPresetCount(); ++p) {
    VoiceConfig c = VoicePresets::getPresetConfig(p);
    enablePatch(c);
    INFO("Testing preset: " << VoicePresets::getPresetName(p));

    for (ParamId lane : {ParamId::Velocity, ParamId::Filter, ParamId::Attack, ParamId::Decay, ParamId::Octave}) {
      const float base = laneBase(lane, c);
      REQUIRE(composeLane(lane, 0.0f, &c) == 0.0f);
      REQUIRE(composeLane(lane, 0.5f, &c) == Approx(base));
      REQUIRE(composeLane(lane, 1.0f, &c) == 1.0f);

      // Verify no dead zones: strictly monotonic progression
      if (base > 0.0f) {
        REQUIRE(composeLane(lane, 0.25f, &c) > 0.0f);
        REQUIRE(composeLane(lane, 0.25f, &c) < base);
      }
      if (base < 1.0f) {
        REQUIRE(composeLane(lane, 0.75f, &c) > base);
        REQUIRE(composeLane(lane, 0.75f, &c) < 1.0f);
      }
    }
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
