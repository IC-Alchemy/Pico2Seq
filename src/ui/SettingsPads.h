#pragma once

#include "../voice/VoiceEditParameters.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// SettingsPads.h — which voice-timbre pad does what (portable, no hardware).
// Same 32-pad catalogue drives edits, LED levels, and OLED labels so the three
// can never disagree. Pads are fixed banks (source/filter/env/output), never
// remapped through the step PadBank. Shift = decrease/step-back.
namespace SettingsPads {
inline constexpr uint8_t kPadCount = 32;

inline constexpr VoiceEdit::Id parameter(uint8_t pad) noexcept {
  using VoiceEdit::Id;
  // Raw pad -> timbre parameter. Fixed order: source, filter/drive,
  // envelope/filter, output/glide/macros. Out-of-range = Count (no-op).
  constexpr Id ids[kPadCount] = {
      Id::Enabled, Id::Gate, Id::Slide, Id::RecipeRetrigger,
      Id::FilterType, Id::Engine, Id::OscCount, Id::Recipe,
      Id::EnvelopeOn, Id::DriveOn, Id::FilterOn, Id::FilterMode,
      Id::Resonance, Id::StaticCutoff, Id::Drive, Id::DriveGain,
      Id::EnvAttack, Id::EnvDecay, Id::Sustain, Id::Release,
      Id::HighPassFreq, Id::HighPassRes, Id::FilterDrive, Id::Passband,
      Id::Output, Id::SlideTime, Id::Macro1, Id::Macro2,
      Id::Macro3, Id::FilterEnvAmount, Id::FilterEnvFloor, Id::NoiseLevel};
  return pad < kPadCount ? ids[pad] : Id::Count;
}

inline bool available(uint8_t pad, const VoiceConfig &config) noexcept {
  return VoiceEdit::available(parameter(pad), config);
}

namespace detail {
inline float choiceMaximum(VoiceEdit::Id id, const VoiceConfig &config) noexcept {
  // VoiceEdit exposes SVF responses as three consecutive choices, then maps
  // them to the shared LP/BP/HP enum (0/2/4) in setValue().
  return id == VoiceEdit::Id::FilterMode && config.filterType == FILTER_SVF
             ? 2.0f
             : VoiceEdit::parameter(id).maximum;
}
} // namespace detail

// True = handled (even if already at the limit, so the OLED still confirms).
// Toggles flip, choices wrap, numerics nudge; unavailable pads return false.
inline bool apply(uint8_t pad, VoiceConfig &config, bool decrease) noexcept {
  const auto id = parameter(pad);
  if (!VoiceEdit::available(id, config))
    return false;
  const auto &p = VoiceEdit::parameter(id);
  if (p.unit == VoiceEdit::Unit::Toggle) {
    VoiceEdit::setValue(id, config, VoiceEdit::value(id, config) != 0 ? 0 : 1);
  } else if (p.unit == VoiceEdit::Unit::Choice) {
    const float maximum = detail::choiceMaximum(id, config);
    float next = VoiceEdit::value(id, config) + (decrease ? -1.0f : 1.0f);
    if (next < p.minimum)
      next = maximum;
    else if (next > maximum)
      next = p.minimum;
    VoiceEdit::setValue(id, config, next);
    if (id == VoiceEdit::Id::FilterType && config.filterType == FILTER_SVF)
      VoiceEdit::setValue(VoiceEdit::Id::FilterMode, config,
                          VoiceEdit::value(VoiceEdit::Id::FilterMode, config));
  } else {
    // Stepped numerics (e.g. osc count) nudge in 5% moves for encoder-like feel.
    VoiceEdit::adjust(id, config, decrease ? -0.05f : 0.05f);
  }
  return true;
}

// 0..1 LED/oled level for a pad: sequencer-bound lanes use the live binding,
// others use their (log-aware) min/max mapping. NaN/unavailable reads as 0.
inline float level(uint8_t pad, const VoiceConfig &config) noexcept {
  auto id = parameter(pad);
  if (!VoiceEdit::available(id, config))
    return 0.0f;
  // Envelope Attack aliases the shorter sequencer attack range when tracked.
  if (id == VoiceEdit::Id::EnvAttack &&
      VoiceEdit::sequenceLane(id, config) == ParamId::Attack)
    id = VoiceEdit::Id::Attack;
  const float value = VoiceEdit::value(id, config);
  if (!std::isfinite(value))
    return 0.0f;
  const auto lane = VoiceEdit::sequenceLane(id, config);
  if (lane != ParamId::Count) {
    const auto &binding = VoiceParameters::binding(config, lane);
    if (binding.target)
      return std::clamp(binding.normalize(value), 0.0f, 1.0f);
  }
  const auto &p = VoiceEdit::parameter(id);
  const float maximum = detail::choiceMaximum(id, config);
  const float clamped = std::clamp(value, p.minimum, maximum);
  if (maximum <= p.minimum)
    return 0.0f;
  if (p.logarithmic)
    return std::clamp(std::log(clamped / p.minimum) /
                          std::log(maximum / p.minimum),
                      0.0f, 1.0f);
  return (clamped - p.minimum) / (maximum - p.minimum);
}

// Notice window for the timbre banner; unsigned math survives millis() wrap.
// Caller owns notice validity; unsigned subtraction survives millis() wrap.
inline constexpr bool noticeActive(uint32_t now, uint32_t changedAt) noexcept {
  return static_cast<uint32_t>(now - changedAt) < 3000u;
}
} // namespace SettingsPads
