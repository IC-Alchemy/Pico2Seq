#pragma once

#include "../rpdsp/src/rpdsp/oscillator.h"

#include <cstdint>
#include <type_traits>
#include <variant>

// Waveform identifiers stored in VoiceConfig::oscWaveforms[]. rpdsp uses one
// class per waveform instead of a waveform enum, so these ids select the
// class through VoiceOscillator below. WAVE_NOISE stays at 255, the old
// VoiceConfig percussion/noise marker.
inline constexpr uint8_t WAVE_SIN = 0;
inline constexpr uint8_t WAVE_TRI = 1;
inline constexpr uint8_t WAVE_SAW = 2;
inline constexpr uint8_t WAVE_SQUARE = 3;
inline constexpr uint8_t WAVE_BSP_SAW = 4;     // band-limited (was WAVE_POLYBLEP_SAW)
inline constexpr uint8_t WAVE_BSP_SQUARE = 5;  // band-limited (was WAVE_POLYBLEP_SQUARE)
inline constexpr uint8_t WAVE_NOISE = 255;

// One oscillator slot in a Voice: decouples the waveform byte in VoiceConfig
// from rpdsp's class-per-waveform API. Amplitude is not modeled here — the
// caller multiplies oscAmplitudes[] at mix time because rpdsp oscillators
// have no amp parameter.
class VoiceOscillator {
 public:
  void prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    std::visit([this](auto& osc) { prepareIfTuned(osc); }, osc_);
  }

  // Swaps the active oscillator class. The running pitch and pulse width are
  // re-applied so a config commit mid-note does not drop the frequency.
  void setWaveform(uint8_t waveform) {
    const uint8_t normalized = normalize(waveform);
    if (normalized == waveform_) {
      return;
    }
    waveform_ = normalized;
    osc_ = makeOscillator(normalized);
    std::visit([this](auto& osc) {
      prepareIfTuned(osc);
      setFreqIfTuned(osc);
      setPwmIfPulse(osc);
    }, osc_);
  }

  void setFreq(float hz) {
    freqHz_ = hz;
    std::visit([this](auto& osc) { setFreqIfTuned(osc); }, osc_);
  }

  void setPulseWidth(float width) {
    pulseWidth_ = width;
    std::visit([this](auto& osc) { setPwmIfPulse(osc); }, osc_);
  }

  float process() {
    return std::visit([](auto& osc) { return osc.process(); }, osc_);
  }

  uint8_t waveform() const { return waveform_; }

 private:
  using Osc = std::variant<rpdsp::BSplineSawOsc,
                          rpdsp::BSplineSquareOsc,
                          rpdsp::SineOscillator, rpdsp::TriangleOscillator,
                          rpdsp::SawOsc, rpdsp::SquareOsc,
                          rpdsp::NoiseOscillator>;

  template <typename T, typename = void>
  struct HasPwm : std::false_type {};
  template <typename T>
  struct HasPwm<T, std::void_t<decltype(std::declval<T&>().setPWM(
                       std::declval<float>()))>> : std::true_type {};

  static uint8_t normalize(uint8_t waveform) {
    switch (waveform) {
      case WAVE_SIN:
      case WAVE_TRI:
      case WAVE_SAW:
      case WAVE_SQUARE:
      case WAVE_BSP_SAW:
      case WAVE_BSP_SQUARE:
      case WAVE_NOISE:
        return waveform;
      default:
        // Unknown ids fall back to the workhorse band-limited saw, matching
        // the old Oscillator's clamp-to-valid behavior.
        return WAVE_BSP_SAW;
    }
  }

  static Osc makeOscillator(uint8_t waveform) {
    switch (waveform) {
      case WAVE_SIN:
        return rpdsp::SineOscillator{};
      case WAVE_TRI:
        return rpdsp::TriangleOscillator{};
      case WAVE_SAW:
        return rpdsp::SawOsc{};
      case WAVE_SQUARE:
        return rpdsp::SquareOsc{};
      case WAVE_BSP_SQUARE:
        return rpdsp::BSplineSquareOsc{};
      case WAVE_NOISE:
        return rpdsp::NoiseOscillator{};
      case WAVE_BSP_SAW:
      default:
        return rpdsp::BSplineSawOsc{};
    }
  }

  template <typename T>
  void prepareIfTuned(T& osc) {
    if constexpr (!std::is_same_v<T, rpdsp::NoiseOscillator>) {
      osc.prepare(sampleRate_);
    }
  }

  template <typename T>
  void setFreqIfTuned(T& osc) {
    if constexpr (!std::is_same_v<T, rpdsp::NoiseOscillator>) {
      osc.setFreq(freqHz_);
    }
  }

  template <typename T>
  void setPwmIfPulse(T& osc) {
    if constexpr (HasPwm<T>::value) {
      osc.setPWM(pulseWidth_);
    }
  }

  float sampleRate_ = 48000.0f;
  float freqHz_ = 440.0f;
  float pulseWidth_ = 0.5f;
  uint8_t waveform_ = WAVE_BSP_SAW;
  Osc osc_{rpdsp::BSplineSawOsc{}};
};
