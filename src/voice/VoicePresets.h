// VoicePresets.h — factory patches (flash-resident constexpr; no heap).
// Each preset is a complete musical starting point: engine, oscillators,
// filter, and envelope defaults voiced to play well from the first step.
#pragma once

#include "VoiceConfig.h"
#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <string_view>
#include <cstdint>

namespace VoicePresets {
  enum class Id : uint8_t {
#define VOICE_PRESET(id, name, factory) id,
#include "presets/PresetBank.h"
#undef VOICE_PRESET
    Count
  };
  // Settings-mode preset browser: pad N applies preset N on pads 0-30; pad 31
  // is unassigned. The whole bank fits (checked in VoicePresets.cpp), so the
  // browser has no pages.
  inline constexpr uint8_t kPresetPadCount = 31;
  int findPreset(std::string_view name) noexcept;
  const VoiceConfig &getPresetConfigByName(std::string_view name) noexcept;

  // Factory functions for common synthesizer voice types
  const VoiceConfig& getAnalogVoice() noexcept;
  const VoiceConfig& getDigitalVoice() noexcept;
  const VoiceConfig& getBassVoice() noexcept;
  const VoiceConfig& getLeadVoice() noexcept;
  const VoiceConfig& getSquareVoice() noexcept;
  const VoiceConfig& getPadVoice() noexcept;
  const VoiceConfig& getPercussionVoice() noexcept;

  // Extended factories: subs, plucked strings, supersaw, noise, recipes.
  const VoiceConfig& getSubFunkVoice() noexcept;
  const VoiceConfig& getRubberSubVoice() noexcept;
  const VoiceConfig& getWaveguidePluckVoice() noexcept;
  const VoiceConfig& getWaveguideNylonVoice() noexcept;
  const VoiceConfig& getWaveguideBellVoice() noexcept;
  const VoiceConfig& getWaveguideShimmerVoice() noexcept;
  const VoiceConfig& getHypersawVoice() noexcept;
  const VoiceConfig& getNoiseStormVoice() noexcept;
  const VoiceConfig& getSitarVoice() noexcept;

  // Preset utilities
  const char* getPresetName(uint8_t presetIndex) noexcept;
  const VoiceConfig& getPresetConfig(uint8_t presetIndex) noexcept;
  uint8_t getPresetCount() noexcept;

  // Inverse of the WgPluck T60 lane (0.15..4 s, centered 1.8 s): converts a
  // patch T60 in seconds back to a Decay-lane value for track seeding.
  float wgT60ToNormalized(float t60Seconds) noexcept;

  // Which sequencer parameter set a preset exposes. Out-of-range or unknown
  // indexes fall back to PARAMSET_STANDARD.
  VoiceParamSet getPresetParamSet(uint8_t presetIndex) noexcept;

  // Display name for a sequencer slot under the preset's param set, or
  // nullptr when the slot keeps its standard meaning (caller falls back to
  // paramName()). Names are short enough for the OLED's size-2 text.
  const char* getSequencerParamName(uint8_t presetIndex, ParamId id) noexcept;

  // Preset index a browser pad applies, or -1 when the pad holds no preset.
  int presetIndexForPad(uint8_t padIndex, uint8_t presetCount) noexcept;
}