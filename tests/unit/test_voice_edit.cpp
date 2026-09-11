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
  REQUIRE(seq.getStepParameterValue(ParamId::Note, step) == 18);
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
  REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 18);
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
  REQUIRE(seq.getStepParameterValue(ParamId::Note, 0) == 18);
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
  for (int i = 0; i < 1024; ++i)
    REQUIRE(manager.processAllVoices() == 0);
  for (uint8_t i = 0; i < 4; ++i)
    REQUIRE(manager.getVoiceConfig(ids[i])->baseVelocity ==
            Approx(0.1f * (i + 1)));
}
