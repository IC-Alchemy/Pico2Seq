#include "src/ui/VoiceEditControls.h"
#include "src/voice/VoiceEditParameters.h"
#include "src/voice/VoiceManager.h"
#include "src/voice/VoicePresets.h"
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
  REQUIRE(state.velocityLevel == Approx(0.85f));
  REQUIRE(config.baseVelocity == Approx(0.6f));
  config.baseVelocity = 0.4f;
  seq.playStepNow(step, &state);
  REQUIRE(state.velocityLevel == Approx(0.65f));
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
  REQUIRE(composeLane(ParamId::Velocity, 0.25f, &sync) == Approx(0.5f));
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
  REQUIRE(state.velocityLevel == Approx(0.45f));
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
  for (int i = 0; i < 4; ++i) {
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

TEST_CASE("Patch randomization preserves register and playable preset timing", "[voice_edit][recording]") {
  auto c = VoicePresets::getDigitalVoice();
  enablePatch(c);
  Sequencer seq;
  seedModifiers(seq);
  seq.setPlaybackTransform(composeLane, &c, mapOctave);
  for (uint8_t lane = 0; lane < PARAM_ID_COUNT; ++lane)
    seq.setParameterStepCount(static_cast<ParamId>(lane), 64);
  for (int run = 0; run < 4; ++run) {
    seq.randomizeParameters();
    for (uint8_t i = 0; i < 64; ++i) {
      auto step = seq.getPlaybackStep(i);
      REQUIRE(step.noteIndex >= 0);
      REQUIRE(step.noteIndex <= 12);
      REQUIRE(step.noteIndex == std::round(step.noteIndex));
      REQUIRE(step.octaveOffset == 0);
      REQUIRE(step.gateLengthTicks == 60);
      REQUIRE(MusicalValues::envelopeSeconds(step.attackTimeSeconds) >= c.defaultAttack * 0.6f);
      REQUIRE(MusicalValues::envelopeSeconds(step.attackTimeSeconds) <= c.defaultAttack * 1.6f);
      REQUIRE(MusicalValues::envelopeSeconds(step.decayTimeSeconds) >= c.defaultDecay * 0.6f);
    }
  }
}
