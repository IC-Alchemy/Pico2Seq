#pragma once
#include "VoiceEditParameters.h"
#include "../pico2seq-core/scales/scales.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// Shared audio/display conversions. No UI state, allocation or hardware access.
namespace MusicalValues {
inline int midiNote(float note, int octave, int harmony, const int *row) noexcept {
  const int index = std::clamp(static_cast<int>(note) + harmony, 0, int(SCALE_STEPS) - 1);
  return std::clamp(48 + (row ? row[index] : index) + octave, 0, 127);
}
inline float envelopeSeconds(float normalized) noexcept {
  return 0.001f * std::pow(10000.0f, std::clamp(normalized, 0.0f, 1.0f));
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
    return VoiceEdit::composeLane(id, id == ParamId::Note ? 0.0f :
        id == ParamId::Gate ? 1.0f : id == ParamId::Slide ? 0.0f :
        mapNormalizedValueToParamRange(id, 0.5f), &config);
  };
  Step s;
  s.noteIndex = lane(ParamId::Note);
  s.velocityLevel = lane(ParamId::Velocity);
  s.filterCutoff = lane(ParamId::Filter);
  s.attackTimeSeconds = lane(ParamId::Attack);
  s.decayTimeSeconds = lane(ParamId::Decay);
  s.octaveOffset = VoiceEdit::mapOctave(lane(ParamId::Octave));
  s.isGateActive = lane(ParamId::Gate) > 0.5f;
  s.hasSlide = lane(ParamId::Slide) > 0.5f;
  s.gateLengthTicks = static_cast<uint16_t>(std::max(1.0f, lane(ParamId::GateLength) *
      SequencerConstants::PULSES_PER_SEQUENCER_STEP_TICKS));
  return s;
}
inline void format(ParamId id, const Step &step, const VoiceConfig &config,
                   const int *row, float bpm, char *out, size_t size) noexcept {
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
  default: std::snprintf(out, size, "--"); return;
  }
  const auto &binding = VoiceParameters::binding(config, id);
  if (id == ParamId::Filter && !binding.target && !config.hasFilter) {
    std::snprintf(out, size, "Bypass"); return;
  }
  if ((id == ParamId::Attack || id == ParamId::Decay) && !binding.target) {
    if (!config.hasEnvelope) { std::snprintf(out, size, "Off"); return; }
    float seconds;
    if (!VoiceParameters::layout(config).envelopeFromTracks)
      seconds = id == ParamId::Attack ? config.defaultAttack : config.defaultDecay;
    else if (config.usePatchBases) seconds = envelopeSeconds(normalized);
    else if (id == ParamId::Attack) seconds = dspmap::fmap(normalized, 0.002f, 0.75f, dspmap::Mapping::LINEAR);
    else seconds = 0.075f + 0.32f * dspmap::fmap(normalized, 0.01f, 0.5f, dspmap::Mapping::LOG);
    time(seconds, out, size); return;
  }
  if (VoiceParameters::formatValue(config, id, normalized, out, size)) return;
  // Velocity is an amplitude multiplier, not a MIDI velocity byte.
  std::snprintf(out, size, "%.2fx", normalized);
}
} // namespace MusicalValues
