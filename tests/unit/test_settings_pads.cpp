#include "src/ui/SettingsPads.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using Catch::Approx;
using VoiceEdit::Id;

TEST_CASE("Settings pads retain their raw 32-pad parameter map", "[settings_pads]") {
  constexpr Id expected[] = {
      Id::Enabled, Id::Gate, Id::Slide, Id::RecipeRetrigger,
      Id::FilterType, Id::Engine, Id::OscCount, Id::Recipe,
      Id::EnvelopeOn, Id::DriveOn, Id::FilterOn, Id::FilterMode,
      Id::Resonance, Id::StaticCutoff, Id::Drive, Id::DriveGain,
      Id::EnvAttack, Id::EnvDecay, Id::Sustain, Id::Release,
      Id::HighPassFreq, Id::HighPassRes, Id::FilterDrive, Id::Passband,
      Id::Output, Id::SlideTime, Id::Macro1, Id::Macro2,
      Id::Macro3, Id::FilterEnvAmount, Id::FilterEnvFloor, Id::NoiseLevel};
  STATIC_REQUIRE(SettingsPads::kPadCount == 32);
  STATIC_REQUIRE(SettingsPads::parameter(0) == Id::Enabled);
  VoiceConfig config;
  for (unsigned pad = 0; pad < 256; ++pad) {
    CAPTURE(pad);
    if (pad < SettingsPads::kPadCount) {
      REQUIRE(SettingsPads::parameter(pad) == expected[pad]);
    } else {
      REQUIRE(SettingsPads::parameter(pad) == Id::Count);
      REQUIRE_FALSE(SettingsPads::available(pad, config));
      REQUIRE_FALSE(SettingsPads::apply(pad, config, false));
      REQUIRE_FALSE(SettingsPads::apply(pad, config, true));
      REQUIRE(SettingsPads::level(pad, config) == 0.0f);
    }
  }
}

TEST_CASE("Settings availability follows the voice editor", "[settings_pads]") {
  for (uint8_t engine = ENGINE_OSC; engine <= ENGINE_RECIPE; ++engine) {
    for (bool enabled : {false, true}) {
      for (auto topology : {FILTER_LADDER, FILTER_SVF}) {
        VoiceConfig config;
        VoiceEdit::setValue(Id::Engine, config, engine);
        config.hasEnvelope = config.hasFilter = config.hasOverdrive = enabled;
        config.filterType = topology;
        for (uint8_t pad = 0; pad < SettingsPads::kPadCount; ++pad) {
          CAPTURE(engine, enabled, topology, pad);
          const auto id = SettingsPads::parameter(pad);
          const bool available = VoiceEdit::available(id, config);
          REQUIRE(SettingsPads::available(pad, config) == available);
          if (!available) {
            const float before = VoiceEdit::value(id, config);
            REQUIRE_FALSE(SettingsPads::apply(pad, config, false));
            REQUIRE_FALSE(SettingsPads::apply(pad, config, true));
            REQUIRE(VoiceEdit::value(id, config) == before);
            REQUIRE(SettingsPads::level(pad, config) == 0.0f);
          }
        }
      }
    }
  }
}

TEST_CASE("Settings toggles flip in either edit direction", "[settings_pads]") {
  for (uint8_t pad : {0, 1, 2, 3, 8, 9, 10}) {
    for (bool decrease : {false, true}) {
      VoiceConfig config;
      VoiceEdit::setValue(Id::Engine, config, ENGINE_RECIPE);
      const auto id = SettingsPads::parameter(pad);
      CAPTURE(pad, decrease);
      VoiceEdit::setValue(id, config, 0);
      REQUIRE(SettingsPads::level(pad, config) == 0.0f);
      REQUIRE(SettingsPads::apply(pad, config, decrease));
      REQUIRE(VoiceEdit::value(id, config) == 1.0f);
      REQUIRE(SettingsPads::level(pad, config) == 1.0f);
      REQUIRE(SettingsPads::apply(pad, config, decrease));
      REQUIRE(VoiceEdit::value(id, config) == 0.0f);
    }
  }
}

TEST_CASE("Settings choices cycle and wrap in both directions", "[settings_pads]") {
  for (uint8_t pad : {4, 5, 7, 11}) {
    for (auto topology : {FILTER_LADDER, FILTER_SVF}) {
      for (bool decrease : {false, true}) {
        VoiceConfig config;
        VoiceEdit::setValue(Id::Engine, config, ENGINE_RECIPE);
        config.hasFilter = true;
        config.filterType = topology;
        const auto id = SettingsPads::parameter(pad);
        const auto &p = VoiceEdit::parameter(id);
        const int maximum = id == Id::FilterMode && topology == FILTER_SVF
                                ? 2 : static_cast<int>(p.maximum);
        const int minimum = static_cast<int>(p.minimum);
        for (int start = minimum; start <= maximum; ++start) {
          CAPTURE(pad, topology, decrease, start);
          VoiceEdit::setValue(id, config, start);
          int expected = start + (decrease ? -1 : 1);
          if (expected < minimum) expected = maximum;
          if (expected > maximum) expected = minimum;
          REQUIRE(SettingsPads::apply(pad, config, decrease));
          REQUIRE(VoiceEdit::value(id, config) == expected);
          REQUIRE(SettingsPads::level(pad, config) ==
                  Approx(float(expected - minimum) / float(maximum - minimum)));
          if (id == Id::Engine && config.engine == ENGINE_RECIPE) {
            REQUIRE(config.recipe != nullptr);
            REQUIRE(config.parameters != nullptr);
          }
          if (id == Id::Recipe) {
            REQUIRE(config.recipe != nullptr);
            REQUIRE(config.parameters != nullptr);
          }
        }
      }
    }
  }
}

TEST_CASE("Switching to SVF canonicalizes all ladder responses", "[settings_pads]") {
  for (int mode = 0; mode < 6; ++mode) {
    VoiceConfig config;
    config.hasFilter = true;
    config.filterType = FILTER_LADDER;
    VoiceEdit::setValue(Id::FilterMode, config, mode);
    REQUIRE(SettingsPads::apply(4, config, false));
    REQUIRE(config.filterType == FILTER_SVF);
    REQUIRE(static_cast<int>(config.filterMode) == (mode / 2) * 2);
  }
}

TEST_CASE("Settings numeric edits delegate to normalized voice adjustments",
          "[settings_pads]") {
  for (uint8_t engine = ENGINE_OSC; engine <= ENGINE_RECIPE; ++engine) {
    for (bool decrease : {false, true}) {
      VoiceConfig base;
      VoiceEdit::setValue(Id::Engine, base, engine);
      base.hasEnvelope = base.hasFilter = base.hasOverdrive = true;
      for (uint8_t pad = 0; pad < SettingsPads::kPadCount; ++pad) {
        const auto id = SettingsPads::parameter(pad);
        const auto unit = VoiceEdit::parameter(id).unit;
        if (!SettingsPads::available(pad, base) ||
            unit == VoiceEdit::Unit::Toggle || unit == VoiceEdit::Unit::Choice)
          continue;
        CAPTURE(engine, decrease, pad);
        auto actual = base;
        auto expected = base;
        for (int press = 0; press < 30; ++press) {
          VoiceEdit::adjust(id, expected, decrease ? -0.05f : 0.05f);
          REQUIRE(SettingsPads::apply(pad, actual, decrease));
          REQUIRE(VoiceEdit::value(id, actual) == Approx(VoiceEdit::value(id, expected)));
          REQUIRE(SettingsPads::level(pad, actual) >= 0.0f);
          REQUIRE(SettingsPads::level(pad, actual) <= 1.0f);
        }
      }
    }
  }
}

TEST_CASE("Settings levels use linear logarithmic and bound lane scales",
          "[settings_pads]") {
  VoiceConfig config;
  config.hasEnvelope = config.hasFilter = true;
  VoiceEdit::setValue(Id::Output, config, 0.35f);
  REQUIRE(SettingsPads::level(24, config) == Approx(0.35f));
  config.highPassFreq = std::sqrt(20.0f * 20000.0f);
  REQUIRE(SettingsPads::level(20, config) == Approx(0.5f));
  config.defaultAttack = std::sqrt(0.001f * VoiceEdit::kAttackMaxSeconds);
  REQUIRE(SettingsPads::level(16, config) == Approx(0.5f));
  config.defaultAttack = VoiceEdit::kAttackMaxSeconds;
  REQUIRE(SettingsPads::level(16, config) == Approx(1.0f));

  VoiceParameterLayout layout;
  layout.envelopeFromTracks = false;
  layout.slots[static_cast<size_t>(ParamId::Filter)] = {
      "Macro", &VoiceConfig::macro2, 1.0f, 100.0f, dspmap::Mapping::EXP,
      VoiceParameterUnit::Ratio, true, 0.5f, 4.0f};
  VoiceEdit::setValue(Id::Engine, config, ENGINE_RECIPE);
  config.parameters = &layout;
  config.defaultAttack = std::sqrt(0.001f * 10.0f);
  REQUIRE(SettingsPads::level(16, config) == Approx(0.5f));
  const auto &binding = VoiceParameters::binding(config, ParamId::Filter);
  for (float normalized : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
    config.macro2 = binding.map(normalized);
    REQUIRE(SettingsPads::level(27, config) == Approx(normalized).margin(0.00001f));
  }
  config.macro2 = binding.map(0.5f);
  REQUIRE(SettingsPads::apply(27, config, false));
  REQUIRE(SettingsPads::level(27, config) == Approx(0.55f));

  config.highPassFreq = -1.0f;
  REQUIRE(SettingsPads::level(20, config) == 0.0f);
  config.highPassFreq = 30000.0f;
  REQUIRE(SettingsPads::level(20, config) == 1.0f);
  config.highPassFreq = std::numeric_limits<float>::quiet_NaN();
  REQUIRE(SettingsPads::level(20, config) == 0.0f);
}

TEST_CASE("Settings notices expire at three seconds across clock wrap",
          "[settings_pads]") {
  for (uint32_t changedAt : {0u, 10000u, UINT32_MAX - 1000u}) {
    REQUIRE(SettingsPads::noticeActive(changedAt, changedAt));
    REQUIRE(SettingsPads::noticeActive(changedAt + 2999u, changedAt));
    REQUIRE_FALSE(SettingsPads::noticeActive(changedAt + 3000u, changedAt));
    REQUIRE_FALSE(SettingsPads::noticeActive(changedAt + 4000u, changedAt));
  }
}
