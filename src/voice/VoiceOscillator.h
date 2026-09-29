#pragma once

#include "../rpdsp/src/rpdsp/oscillator.h"
#include "../utils/AudioRam.h"

#include <cstdint>
#include <type_traits>
#include <variant>

// Waveform ids in VoiceConfig::oscWaveforms[]. WAVE_NOISE (255) is the legacy
// noise marker; unknown ids fall back to the band-limited saw below.
inline constexpr uint8_t WAVE_SIN = 0;
inline constexpr uint8_t WAVE_TRI = 1;
inline constexpr uint8_t WAVE_SAW = 2;
inline constexpr uint8_t WAVE_SQUARE = 3;
inline constexpr uint8_t WAVE_BSP_SAW = 4;     // band-limited (was WAVE_POLYBLEP_SAW)
inline constexpr uint8_t WAVE_BSP_SQUARE = 5;  // band-limited (was WAVE_POLYBLEP_SQUARE)
inline constexpr uint8_t WAVE_HARDSYNC_SAW = 6; // band-limited master/slave hard-sync saw
inline constexpr uint8_t WAVE_NOISE = 255;

// VoiceOscillator.h — one oscillator slot: maps the WAVE_* id in VoiceConfig
// to rpdsp's class-per-waveform API. Amplitude stays with the caller (mix-time
// oscAmplitudes[] gain); slave pitch is only read for WAVE_HARDSYNC_SAW.
// Hot path: variant dispatch runs once per span; no allocation here.
class VoiceOscillator {
 public:
  void prepare(float sampleRate) {
    sampleRate_ = sampleRate;
    std::visit([this](auto& osc) { prepareIfTuned(osc); }, osc_);
  }

  // Swaps the oscillator class for a waveform edit. Pitch/pulse width carry
  // over so a mid-note edit never drops the frequency.
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
      setSlaveFreqIfHardSync(osc);
      setPwmIfPulse(osc);
    }, osc_);
  }

  void setFreq(float hz) {
    freqHz_ = hz;
    std::visit([this](auto& osc) { setFreqIfTuned(osc); }, osc_);
  }

  // WAVE_HARDSYNC_SAW only: independent slave pitch (the sequenced Slave lane
  // rides this); every other waveform ignores it.
  void setSlaveFrequency(float hz) {
    slaveFrequencyHz_ = hz;
    std::visit([this](auto& osc) { setSlaveFreqIfHardSync(osc); }, osc_);
  }

  float masterFrequency() const { return freqHz_; }
  float slaveFrequency() const { return slaveFrequencyHz_; }

  void setPulseWidth(float width) {
    pulseWidth_ = width;
    std::visit([this](auto& osc) { setPwmIfPulse(osc); }, osc_);
  }

  float process() {
    return std::visit([](auto& osc) { return osc.process(); }, osc_);
  }

  // One variant dispatch per span. Preserve oscillator summation order and
  // freeze source state on the same envelope-silenced samples as process().
  // Process the stored oscillator directly: a copied B-spline saw changes
  // GCC's contraction of its integrator and fails the PCM16 comparison.
  void renderAdd(float *mix, const float *env, uint32_t n, float amp, bool gateBySilence) noexcept {
    std::visit([&](auto& osc) {
      renderOscillatorAdd_(osc, mix, env, n, amp, gateBySilence);
    }, osc_);
  }

  uint8_t waveform() const { return waveform_; }

 private:
  // Keep the sample loops in SRAM even when std::visit emits an out-of-line
  // dispatch helper in flash. Dispatch itself happens only once per span.
  template <typename T>
#if defined(__GNUC__)
  __attribute__((noinline))
#endif
  static void PICO2SEQ_AUDIO_FUNC(renderOscillatorAdd_)(
      T &osc, float *mix, const float *env, uint32_t n, float amp, bool gateBySilence) noexcept {
    for (uint32_t k = 0; k < n; ++k) {
      if (gateBySilence && env[k] <= 0.001f) continue;
      // Round the waveform before gain; preserve the scalar dispatch boundary.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 10
      const float source = __builtin_assoc_barrier(osc.process());
#else
      const volatile float source = osc.process();
#endif
      mix[k] += source * amp;
    }
  }

  using Osc = std::variant<rpdsp::BSplineSawOsc,
                           rpdsp::BSplineSquareOsc,
                           rpdsp::SineOscillator, rpdsp::TriangleOscillator,
                           rpdsp::SawOsc, rpdsp::SquareOsc,
                           rpdsp::HardSyncSaw,
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
      case WAVE_HARDSYNC_SAW:
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
      case WAVE_HARDSYNC_SAW:
        return rpdsp::HardSyncSaw{};
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
    if constexpr (std::is_same_v<T, rpdsp::HardSyncSaw>) {
      osc.setMasterFrequency(freqHz_);
    } else if constexpr (!std::is_same_v<T, rpdsp::NoiseOscillator>) {
      osc.setFreq(freqHz_);
    }
  }

  template <typename T>
  void setSlaveFreqIfHardSync(T& osc) {
    if constexpr (std::is_same_v<T, rpdsp::HardSyncSaw>) {
      osc.setSlaveFrequency(slaveFrequencyHz_);
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
  float slaveFrequencyHz_ = 440.0f;
  float pulseWidth_ = 0.5f;
  uint8_t waveform_ = WAVE_BSP_SAW;
  Osc osc_{rpdsp::BSplineSawOsc{}};
};
