#pragma once
#include "VoiceEditParameters.h"
#include "../pico2seq-core/scales/scales.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// MusicalValues.h — shared lane ↔ musical-unit conversions (note names,
// envelope seconds, cutoff Hz) for audio and OLED. Pure math, no UI state,
// no allocation, no hardware access; safe on either core and in tests.
namespace MusicalValues {
inline int midiNote(float note, int octave, int harmony, const int *row) noexcept {
  const int index = std::clamp(static_cast<int>(note) + harmony, 0, int(SCALE_STEPS) - 1);
  return std::clamp(48 + (row ? row[index] : index) + octave, 0, 127);
}
inline float envelopeSeconds(float normalized) noexcept {
  return 0.001f * std::pow(10000.0f, std::clamp(normalized, 0.0f, 1.0f));
}
// Release lane: 10 ms .. 8 s. The four-decade envelopeSeconds() curve put
// every usable release in the top tenth of the lane - lane 0.5 was 100 ms and
// only ~0.85 upward rang for a bar - so a hand sweep felt like it did nothing.
// Under three decades instead, lane 0.5 is 280 ms, 0.75 is 1.5 s, and the top
// of the lane holds a note through 16 steps at any sane tempo.
inline float releaseSeconds(float normalized) noexcept {
  return 0.01f * std::pow(800.0f, std::clamp(normalized, 0.0f, 1.0f));
}
inline float releaseNormalized(float seconds) noexcept {
  const float clamped = std::clamp(seconds, 0.01f, 8.0f);
  return std::log(clamped / 0.01f) / std::log(800.0f);
}
// Attack lane: 1 ms..2 s, so lane 0.5 blooms in ~45 ms (snappy but click-free).
inline float attackSeconds(float normalized) noexcept {
  return 0.001f * std::pow(VoiceEdit::kAttackMaxSeconds / 0.001f, std::clamp(normalized, 0.0f, 1.0f));
}
inline float laneSeconds(ParamId id, float normalized) noexcept {
  if (id == ParamId::Attack)
    return attackSeconds(normalized);
  if (id == ParamId::Release)
    return releaseSeconds(normalized);
  return envelopeSeconds(normalized);
}
inline void time(float seconds, char *out, size_t size) noexcept {
  if (seconds < 1.0f) std::snprintf(out, size, "%.1fms", seconds * 1000);
  else std::snprintf(out, size, "%.2fs", seconds);
}
inline void noteName(float note, int octave, const int *row, char *out, size_t size) noexcept {
  constexpr const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  const int midi = midiNote(note, octave, 0, row);
  std::snprintf(out, size, "%s%d", names[midi % 12], midi / 12 - 1);
}
inline void voiceNotes(const Step &step, const VoiceConfig &config, const int *row,
                       char *out, size_t size) noexcept {
  if (!out || !size) return;
  const bool oscillatorBank = config.engine == ENGINE_OSC;
  const bool enginePitch = config.engine == ENGINE_HYPERSAW || config.engine == ENGINE_RECIPE;
  const uint8_t count = oscillatorBank ? std::min<uint8_t>(config.oscillatorCount, 3) : 1;
  out[0] = 0;
  char previous[24] = "";
  for (uint8_t i = 0; i < count; ++i) {
    if (oscillatorBank && (config.oscAmplitudes[i] <= 0 || config.oscWaveforms[i] == WAVE_NOISE)) continue;
    const int harmony = oscillatorBank || enginePitch ? config.harmony[i] : 0;
    const float detune = oscillatorBank || enginePitch ? config.oscDetuning[i] : 0;
    const float pitch = midiNote(step.noteIndex, step.octaveOffset, harmony, row) + detune;
    const int nearest = static_cast<int>(std::round(pitch));
    const int cents = static_cast<int>(std::round((pitch - nearest) * 100));
    constexpr const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char note[24];
    const int pitchClass = (nearest % 12 + 12) % 12;
    const int octave = (nearest - pitchClass) / 12 - 1;
    if (cents) std::snprintf(note, sizeof(note), "%s%d%+dc", names[pitchClass], octave, cents);
    else std::snprintf(note, sizeof(note), "%s%d", names[pitchClass], octave);
    if (std::strcmp(previous, note) == 0) continue;
    const size_t used = std::strlen(out);
    std::snprintf(out + used, size - used, "%s%s", used ? "/" : "", note);
    std::snprintf(previous, sizeof(previous), "%s", note);
  }
  if (!out[0]) std::snprintf(out, size, "%s", count ? "Noise" : "Silent");
}
inline Step baseStep(const VoiceConfig &config) noexcept {
  const auto lane = [&](ParamId id) {
    return VoiceEdit::composeLane(id, isPatchDefaultLane(id) ? SequencerConstants::LANE_FOLLOWS_PATCH :
        id == ParamId::Note ? 0.0f :
        id == ParamId::Gate ? 1.0f : id == ParamId::Slide ? 0.0f :
        mapNormalizedValueToParamRange(id, 0.5f), &config);
  };
  Step s;
  s.noteIndex = lane(ParamId::Note);
  s.velocityLevel = lane(ParamId::Velocity);
  s.filterCutoff = lane(ParamId::Filter);
  s.attackTimeSeconds = lane(ParamId::Attack);
  s.decayTimeSeconds = lane(ParamId::Decay);
  s.sustainLevel = lane(ParamId::Sustain);
  s.releaseTimeSeconds = lane(ParamId::Release);
  s.octaveOffset = VoiceEdit::mapOctave(lane(ParamId::Octave));
  s.isGateActive = lane(ParamId::Gate) > 0.5f;
  s.hasSlide = lane(ParamId::Slide) > 0.5f;
  s.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f, lane(ParamId::GateLength) *
      SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
  return s;
}
// baseView: the value came from baseStep(), i.e. it is the patch base rather
// than a step's own lane value. It matters for Filter, where the base is a
// cutoff in Hz but the lane is an envelope amount.
inline void format(ParamId id, const Step &step, const VoiceConfig &config,
                   const int *row, float bpm, char *out, size_t size,
                   bool baseView = false) noexcept {
  if (!out || !size) return;
  float normalized = 0;
  switch (id) {
  case ParamId::Note:
    if ((config.engine == ENGINE_OSC && config.oscillatorCount == 0) || config.engine == ENGINE_NOISEFX)
      std::snprintf(out, size, "Noise");
    else voiceNotes(step, config, row, out, size);
    return;
  case ParamId::Octave: std::snprintf(out, size, "%+d oct", step.octaveOffset / 12); return;
  case ParamId::Gate: std::snprintf(out, size, "%s", step.isGateActive ? "On" : "Rest"); return;
  case ParamId::Slide: std::snprintf(out, size, "%s", step.hasSlide ? "On" : "Off"); return;
  case ParamId::GateLength:
    time(step.gateLengthTicks * 60.0f / (std::max(1.0f, bpm) * SequencerConstants::PULSES_PER_QUARTER_NOTE_PPQN), out, size);
    return;
  case ParamId::Velocity: normalized = step.velocityLevel; break;
  case ParamId::Filter: normalized = step.filterCutoff; break;
  case ParamId::Attack: normalized = step.attackTimeSeconds; break;
  case ParamId::Decay: normalized = step.decayTimeSeconds; break;
  case ParamId::Sustain: normalized = step.sustainLevel; break;
  case ParamId::Release: normalized = step.releaseTimeSeconds; break;
  default: std::snprintf(out, size, "--"); return;
  }
  const auto &binding = VoiceParameters::binding(config, id);
  if (id == ParamId::Filter && !binding.target && !config.hasFilter) {
    std::snprintf(out, size, "Bypass"); return;
  }
  if (id == ParamId::Filter && !binding.target && baseView) {
    // The patch cutoff: a frequency, and the floor the envelope opens from.
    std::snprintf(out, size, "%.0fHz",
                  VoiceParameters::mapCutoff(VoiceParameters::layout(config), normalized));
    return;
  }
  if ((id == ParamId::Sustain || id == ParamId::Release) && !binding.target) {
    if (!config.hasEnvelope) { std::snprintf(out, size, "Off"); return; }
    if (id == ParamId::Sustain) std::snprintf(out, size, "%.0f%%", normalized * 100.0f);
    else time(releaseSeconds(normalized), out, size);
    return;
  }
  if ((id == ParamId::Attack || id == ParamId::Decay) && !binding.target) {
    if (!config.hasEnvelope) { std::snprintf(out, size, "Off"); return; }
    float seconds;
    if (!VoiceParameters::layout(config).envelopeFromTracks)
      seconds = id == ParamId::Attack ? config.defaultAttack : config.defaultDecay;
    else if (config.usePatchBases) seconds = laneSeconds(id, normalized);
    else if (id == ParamId::Attack) seconds = dspmap::fmap(normalized, 0.002f, 0.75f, dspmap::Mapping::LINEAR);
    else seconds = 0.075f + 0.32f * dspmap::fmap(normalized, 0.01f, 0.5f, dspmap::Mapping::LOG);
    time(seconds, out, size); return;
  }
  if (VoiceParameters::formatValue(config, id, normalized, out, size)) return;
  if (id == ParamId::Velocity) {
    // Velocity multiplies amplitude (0..1x gain), not a MIDI byte.
    std::snprintf(out, size, "%.2fx", normalized);
    return;
  }
  if (id == ParamId::Filter) {
    std::snprintf(out, size, "%.0fHz",
                  VoiceParameters::mapCutoff(VoiceParameters::layout(config), normalized));
    return;
  }
  if (id == ParamId::Attack || id == ParamId::Decay) {
    // Fallback envelope formatting: if a binding failed to format in VoiceParameters::formatValue,
    // guarantee that Attack/Decay format as musical time (ms/s) rather than a raw float ("%.2f").
    float seconds;
    if (!VoiceParameters::layout(config).envelopeFromTracks)
      // Synthesis engine disables track-driven envelopes (e.g. Waveguide/Hypersaw) -> show static patch default.
      seconds = id == ParamId::Attack ? config.defaultAttack : config.defaultDecay;
    else if (config.usePatchBases)
      // Modern patch-base architecture: laneSeconds() applies percussive curve (Attack: 1ms..2s,
      // ~45ms midpoint) or exponential curve (Decay: 1ms..10s).
      seconds = laneSeconds(id, normalized);
    else if (id == ParamId::Attack)
      // Legacy linear attack mapping: 2 ms .. 750 ms.
      seconds = dspmap::fmap(normalized, 0.002f, 0.75f, dspmap::Mapping::LINEAR);
    else
      // Legacy log decay mapping: 75 ms .. ~235 ms.
      seconds = 0.075f + 0.32f * dspmap::fmap(normalized, 0.01f, 0.5f, dspmap::Mapping::LOG);
    time(seconds, out, size);
    return;
  }
  std::snprintf(out, size, "%.2f", normalized);
}
} // namespace MusicalValues
