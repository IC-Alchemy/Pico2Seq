#include "VoicePresets.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace VoicePresets
{
  // Preset storage: constexpr factories build the whole table at compile time,
  // so it lands in .rodata. On RP2350 (XIP flash) that means the presets cost
  // zero SRAM; every accessor hands out a const reference into flash and the
  // runtime config copies and bounded control queues occupy RAM.
  //
  // Field assignments below are the single source of truth. Fields a factory
  // does not assign keep their VoiceConfig default-member value, exactly like
  // the previous runtime-built table (which value-initialized the same NSDMIs).

  constexpr VoiceConfig makeAnalog() noexcept
  {
    VoiceConfig c{};
    // A single hard-sync pair supplies both the analog-style saw body and a
    // dedicated slave pitch that can be sequenced independently.
    c.oscillatorCount = 1;
    c.oscWaveforms[0] = WAVE_HARDSYNC_SAW;
    c.oscAmplitudes[0] = 1.0f;
    c.oscDetuning[0] = 0.0f;
    c.harmony[0] = 0;          // Root note
    c.paramSet = PARAMSET_HARDSYNC;

    c.filterRes = 0.33f;
    c.filterDrive = 2.1f;
    // Ladder on purpose: this is one of only two presets still using it
    // (with Lead); the test suite pins that count.
    c.filterMode = VoiceFilterMode::LP24;
    c.filterPassbandGain = 0.23f;
    c.highPassFreq = 120.0f;

    c.hasOverdrive = false;
    c.overdriveGain = 0.8f;
    c.overdriveDrive = 0.25f;

    c.defaultAttack = 0.07f;
    c.defaultDecay = 0.24f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = 0.16f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeDigital() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_BSP_SQUARE;
    c.oscWaveforms[1] = // Naive triangle: continuous waveform, band-limited enough without splines
        WAVE_BSP_SQUARE;

    c.oscAmplitudes[0] = .75f;
    c.oscAmplitudes[1] = .65f;
    c.oscDetuning[0] = 0.0f; // Fixed duplicate assignment
    c.oscDetuning[1] = 0.01f;  // Fixed duplicate assignment

    c.harmony[0] = 0; // Root note
    c.filterRes = 0.4f;
    c.filterType = FILTER_SVF; // clean resonant low-pass replaces the ladder
    c.highPassFreq = 111.0f;
    c.highPassRes = 0.15f;
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass

    c.hasOverdrive = false;
    c.overdriveGain = 0.7f;
    c.overdriveDrive = 0.51f;
    c.defaultAttack = 0.015f;
    c.defaultDecay = 0.1f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = 0.15f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeBass() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = // Naive triangle: continuous waveform, band-limited enough without splines
        WAVE_TRI;
    c.oscAmplitudes[0] = 1.f;
    c.oscAmplitudes[1] = 1.f;
    c.oscDetuning[0] = -12.0f;
    c.oscDetuning[1] = 0.0f;
    c.harmony[0] = 0; // Root note
    c.harmony[1] = 0; // Unison (bass typically monophonic)
    c.highPassRes = 0.4f;
    c.filterRes = 0.45f; // SVF resonance carries the growl (no ladder drive)
    c.filterType = FILTER_SVF; // TPT state-variable low-pass: tight, stable bass
    c.highPassFreq = 45.0f; // Lower for bass
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass
    c.hasOverdrive = true;
    c.overdriveGain = 0.95f;
    c.overdriveDrive = 0.16f; // Subtle overdrive
    c.defaultAttack = 0.01f;
    c.defaultDecay = 0.3f;
    c.defaultSustain = 0.85f;
    c.defaultRelease = 0.2f;
    c.outputLevel = .85f;
    return c;
  }

  constexpr VoiceConfig makeLead() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 2;
    c.oscWaveforms[0] = WAVE_BSP_SAW;
    c.oscWaveforms[1] = WAVE_BSP_SAW;
    c.oscAmplitudes[0] = .6f;
    c.oscAmplitudes[1] = .4f;
    c.oscDetuning[0] = 0.0f;
    c.oscDetuning[1] = 0.00f;
    c.harmony[0] = 0; // Root note
    c.harmony[1] = 3; 

    c.filterRes = 0.4f;
    c.filterDrive = 3.f;
    c.filterPassbandGain = 0.23f;
    c.highPassFreq = 160.0f;
    // Ladder on purpose: this is one of only two presets still using it
    // (with Analog); the test suite pins that count.
    c.filterType = FILTER_LADDER; // Use ladder filter for lead
    c.filterMode = VoiceFilterMode::LP12;
    c.hasOverdrive = false;
    c.overdriveGain = 0.7f;
    c.overdriveDrive = 0.45f;

    c.defaultAttack = 0.02f;
    c.defaultDecay = 0.2f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = 0.15f;
    c.outputLevel = 0.5f;
    return c;
  }

  constexpr VoiceConfig makeSquare() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 1;
    c.oscWaveforms[0] = WAVE_BSP_SQUARE;
    c.oscAmplitudes[0] = 1.f;
    c.harmony[0] = 0; // Root note
    c.oscPulseWidth[0] = 0.2f;

    c.filterRes = 0.6f; // 
    c.filterType = FILTER_SVF;
    c.highPassFreq = 150.0f;
    c.filterMode = VoiceFilterMode::BP24; // SVF response: band-pass
    c.hasOverdrive = false;
    c.overdriveGain = 0.75f;
    c.overdriveDrive = 0.35f;

    c.defaultAttack = 0.02f;
    c.defaultDecay = 0.4f;
    c.defaultSustain = 0.0f;
    c.defaultRelease = 0.25f;
    c.outputLevel = .56f;
    return c;
  }

  constexpr VoiceConfig makePad() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_BSP_SAW;
    c.oscWaveforms[1] = WAVE_BSP_SAW;
    c.oscWaveforms[2] = WAVE_BSP_SAW;
    c.oscAmplitudes[0] = 0.33f;
    c.oscAmplitudes[1] = 0.33f;
    c.oscAmplitudes[2] = 0.33f;
    c.harmony[0] = 0;  // Root note
    c.harmony[1] = 4; // Perfect Fifth
    c.harmony[2] = 9;  // Major Third

    c.filterRes = 0.3f;
    c.filterType = FILTER_SVF; // smooth state-variable low-pass
    c.highPassFreq = 140.0f;
    c.highPassRes = 0.08f;
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass

    c.hasOverdrive = false;
    c.overdriveGain = 0.85f;
    c.overdriveDrive = 0.25f;
    c.defaultAttack = 0.02f;   // Slow attack for pad
    c.defaultDecay = 0.2f;
    c.defaultSustain = 0.5f;
    c.defaultRelease = .5f;    // Long release
    c.outputLevel = 0.5f;      // Lower level for pad
    return c;
  }

  constexpr VoiceConfig makePercussion() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;           // No oscillators, only noise
    c.oscWaveforms[0] = WAVE_NOISE;  // Use noise for percussive texture
    c.oscAmplitudes[0] = 1.f;

    c.filterRes = 0.4f;
    c.filterType = FILTER_SVF;
    c.highPassFreq = 200.0f;
    c.filterMode = VoiceFilterMode::LP24; // SVF response: low-pass

    c.hasOverdrive = false;
    c.overdriveGain = 0.45f;
    c.overdriveDrive = 0.3f;

    c.defaultAttack = 0.005f;
    c.defaultDecay = 0.08f;
    c.defaultSustain = 0.0f;
    c.defaultRelease = 0.07f;
    c.outputLevel = 0.5f;
    return c;
  }

  // SubFunk: bouncy sub bass. Sine sub an octave down carries the weight,
  // a triangle adds movement, and a resonant state-variable low-pass with a
  // short punchy envelope gives the filtered-growl funk character.
  constexpr VoiceConfig makeSubFunk() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = WAVE_TRI;
    c.oscWaveforms[2] = WAVE_SIN;
    c.oscAmplitudes[0] = 1.0f;
    c.oscAmplitudes[1] = 0.8f;
    c.oscAmplitudes[2] = 0.65f;
    c.oscDetuning[0] = -12.0f; // sub octave
    c.oscDetuning[1] = -12.0f; // sub octave color
    c.oscDetuning[2] = 0.0f;   // fundamental body
    c.harmony[0] = 0;
    c.harmony[1] = 0;
    c.harmony[2] = 0;

    c.filterRes = 0.5f;
    c.filterType = FILTER_SVF; // resonant low-pass keeps the sub stable under env sweeps
    c.filterMode = VoiceFilterMode::LP12; // SVF response: low-pass
    c.highPassFreq = 55.0f; // keep the sub, shed the rumble

    c.hasOverdrive = true;
    c.overdriveGain = 0.9f;
    c.overdriveDrive = 0.35f; // warm grit

    c.defaultAttack = 0.004f;
    c.defaultDecay = 0.22f;
    c.defaultSustain = 0.35f;
    c.defaultRelease = 0.12f;
    c.outputLevel = 0.9f;
    return c;
  }

  // RubberSub: rubbery sub bass. Sub-octave square grinds under a sine,
  // a resonant state-variable band-pass honks, and harder overdrive spits
  // on transients.
  constexpr VoiceConfig makeRubberSub() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 3;
    c.oscWaveforms[0] = WAVE_SIN;
    c.oscWaveforms[1] = WAVE_BSP_SQUARE;
    c.oscWaveforms[2] = WAVE_TRI;
    c.oscAmplitudes[0] = 0.9f;
    c.oscAmplitudes[1] = 0.3f;
    c.oscAmplitudes[2] = 0.5f;
    c.oscDetuning[0] = -12.0f; // sub octave
    c.oscDetuning[1] = -12.0f; // two octaves down: harmonic grit
    c.oscDetuning[2] = 0.0f;   // fundamental body
    c.harmony[0] = 0;
    c.harmony[1] = 0;
    c.harmony[2] = 0;

    c.filterRes = 0.7f; // SVF band-pass honk (2-pole: higher Q than the old BP24 ladder)
    c.filterType = FILTER_SVF;
    c.filterMode = VoiceFilterMode::BP24; // SVF response: band-pass
    c.highPassFreq = 70.0f;

    c.hasOverdrive = true;
    c.overdriveGain = 1.0f;
    c.overdriveDrive = 0.55f; // spit

    c.defaultAttack = 0.002f;
    c.defaultDecay = 0.16f;
    c.defaultSustain = 0.25f;
    c.defaultRelease = 0.09f;
    c.outputLevel = 0.85f;
    return c;
  }

  // WgPluck: classic Karplus-Strong plucked string (waveguide engine).
  // Bright burst, harmonic loop, short natural tail. No ladder, no ADSR —
  // the string rings out on its own T60; Brightness/Pick/T60 are sequencer
  // slots (see getSequencerParamName).
  constexpr VoiceConfig makeWaveguidePluck() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 1.8f;
    c.wgBrightness = 0.78f;
    c.wgPickPosition = 0.26f;
    c.wgPickHardness = 0.85f;
    c.wgStiffness = 0.0f;
    c.wgDetune = 4.0f;

    c.hasOverdrive = false;
    c.hasFilter = false;    // raw string; velocity scales the output directly
    c.hasEnvelope = false;  // natural T60 decay instead of a gated VCA
    c.highPassFreq = 55.0f;  // sub-shedding HPF: Karplus tails collect rumble
    c.highPassRes = 0.0f;
    c.outputLevel = 0.85f;  // two-string course sums hot
    return c;
  }

  // WgNylon: dark felt-soft nylon string. Damped loop, gentle pick,
  // long sympathetic tail. No ladder, no ADSR.
  constexpr VoiceConfig makeWaveguideNylon() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 3.2f;
    c.wgBrightness = 0.28f;
    c.wgPickPosition = 0.42f;
    c.wgPickHardness = 0.22f;
    c.wgStiffness = 0.05f;
    c.wgDetune = 9.0f;

    c.hasOverdrive = false;
    c.hasFilter = false;
    c.hasEnvelope = false;
    c.highPassFreq = 66.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 0.9f;
    return c;
  }

  // WgBell: stiff dispersive string. High stiffness sharpens upper
  // partials inharmonic (bell/kalimba), hard bridge pick, quick tail.
  // No ladder, no ADSR.
  constexpr VoiceConfig makeWaveguideBell() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 1.4f;
    c.wgBrightness = 0.9f;
    c.wgPickPosition = 0.08f; // bridge: thin and nasal
    c.wgPickHardness = 1.0f;
    c.wgStiffness = 0.88f;    // inharmonic dispersion
    c.wgDetune = 0.0f;

    c.hasOverdrive = false;
    c.hasFilter = false;
    c.hasEnvelope = false;
    c.highPassFreq = 0.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 0.75f;    // bright partials run hot
    return c;
  }

  // WgShimmer: wide detuned two-string course, very long tail. Slow
  // chorusing sustain turns a pluck into a ringing pad. No ladder, no ADSR.
  constexpr VoiceConfig makeWaveguideShimmer() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_WAVEGUIDE;
    c.paramSet = PARAMSET_WAVEGUIDE;
    c.wgT60 = 6.5f;
    c.wgBrightness = 0.55f;
    c.wgPickPosition = 0.35f;
    c.wgPickHardness = 0.6f;
    c.wgStiffness = 0.15f;
    c.wgDetune = 26.0f;       // wide course: slow shimmer

    c.hasOverdrive = false;
    c.hasFilter = false;
    c.hasEnvelope = false;
    c.highPassFreq = 0.0f;
    c.highPassRes = 0.0f;
    c.outputLevel = 0.8f;
    return c;
  }

  // Hypersaw: one native seven-voice Super Saw under a wide-open filter.
  // Detune/Mix are sequencer slots (see getSequencerParamName).
  constexpr VoiceConfig makeHypersaw() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_HYPERSAW;
    c.paramSet = PARAMSET_HYPERSAW;
    c.hypersawDetune = 0.2f;
    c.hypersawMix = 0.5f;

    c.filterRes = 0.35f;
    c.filterType = FILTER_SVF; // wide-open clean low-pass
    c.highPassFreq = 180.0f;
    c.filterMode = VoiceFilterMode::LP24; // SVF response: low-pass

    c.hasOverdrive = false;

    c.defaultAttack = 0.012f;
    c.defaultDecay = 0.3f;
    c.defaultSustain = 0.8f;
    c.defaultRelease = 0.25f;
    c.outputLevel = 0.5f;
    return c;
  }

  // NoiseStorm: noise-based texture engine. Noise plus a pitch-tracked
  // Lorenz chaos growl feed a prime-tap diffuser and a regenerative
  // allpass swarm, then a resonant tracking filter shapes the result.
  // Color/Regen/Chaos are sequencer slots (see getSequencerParamName).
  constexpr VoiceConfig makeNoiseStorm() noexcept
  {
    VoiceConfig c{};
    c.oscillatorCount = 0;
    c.engine = ENGINE_NOISEFX;
    c.paramSet = PARAMSET_NOISESTORM;
    c.noiseDiffuseSize = 0.85f;
    c.noiseDiffuseMix = 0.65f;
    c.noiseSwarmColor = 0.6f;
    c.noiseSwarmRegen = 0.95f;
    c.noiseChaosLevel = 0.4f;

    c.filterRes = 0.72f;      // resonant filter pings with the env
    c.filterType = FILTER_SVF;
    c.highPassFreq = 220.0f;
    c.filterMode = VoiceFilterMode::LP24; // SVF response: low-pass

    c.hasOverdrive = true;
    c.overdriveGain = 0.8f;
    c.overdriveDrive = 0.4f;

    c.defaultAttack = 0.003f;
    c.defaultDecay = 0.5f;
    c.defaultSustain = 0.55f;
    c.defaultRelease = 0.45f;
    c.outputLevel = 0.45f;    // diffuse + swarm can sum hot
    return c;
  }

  // Compile-time table: lives in flash (.rodata), not SRAM.
  static constexpr const char *VOICE_PRESET_NAMES[] = {"Analog", "Digital", "Bass", "Lead", "Square", "Pad", "Percussion", "SubFunk", "RubberSub", "WgPluck", "WgNylon", "WgBell", "WgShimmer", "Hypersaw", "NoiseStorm"};
  static constexpr uint8_t VOICE_PRESET_COUNT = 15;

  constexpr std::array<VoiceConfig, 15> kPresets = {
      makeAnalog(),          makeDigital(),         makeBass(),
      makeLead(),            makeSquare(),          makePad(),
      makePercussion(),      makeSubFunk(),         makeRubberSub(),
      makeWaveguidePluck(),  makeWaveguideNylon(),  makeWaveguideBell(),
      makeWaveguideShimmer(), makeHypersaw(),       makeNoiseStorm()};

  static_assert(kPresets.size() == 15, "preset table and names must stay in sync");
  static_assert(sizeof(VOICE_PRESET_NAMES) / sizeof(VOICE_PRESET_NAMES[0]) == kPresets.size(),
                "preset table and names must stay in sync");

  const VoiceConfig &getAnalogVoice() noexcept { return kPresets[0]; }
  const VoiceConfig &getDigitalVoice() noexcept { return kPresets[1]; }
  const VoiceConfig &getBassVoice() noexcept { return kPresets[2]; }
  const VoiceConfig &getLeadVoice() noexcept { return kPresets[3]; }
  const VoiceConfig &getSquareVoice() noexcept { return kPresets[4]; }
  const VoiceConfig &getPadVoice() noexcept { return kPresets[5]; }
  const VoiceConfig &getPercussionVoice() noexcept { return kPresets[6]; }

  const VoiceConfig &getSubFunkVoice() noexcept { return kPresets[7]; }
  const VoiceConfig &getRubberSubVoice() noexcept { return kPresets[8]; }
  const VoiceConfig &getWaveguidePluckVoice() noexcept { return kPresets[9]; }
  const VoiceConfig &getWaveguideNylonVoice() noexcept { return kPresets[10]; }
  const VoiceConfig &getWaveguideBellVoice() noexcept { return kPresets[11]; }
  const VoiceConfig &getWaveguideShimmerVoice() noexcept { return kPresets[12]; }
  const VoiceConfig &getHypersawVoice() noexcept { return kPresets[13]; }
  const VoiceConfig &getNoiseStormVoice() noexcept { return kPresets[14]; }

  const char *getPresetName(uint8_t presetIndex) noexcept
  {
    if (presetIndex < VOICE_PRESET_COUNT)
    {
      return VOICE_PRESET_NAMES[presetIndex];
    }
    return "Unknown";
  }

  float wgT60ToNormalized(float t60Seconds) noexcept
  {
    // Inverse of dspmap::fmap(v, 0.05f, 10.0f, Mapping::EXP), whose EXP curve
    // is min + in^2 * (max - min): in = sqrt((out - min) / (max - min)).
    const float t = std::clamp(t60Seconds, 0.05f, 10.0f);
    return std::sqrt((t - 0.05f) / 9.95f);
  }

  namespace
  {
    // Re-purposed slot display names per param set; nullptr = slot keeps its
    // standard meaning. Order matters only for readability.
    struct SlotName
    {
      ParamId id;
      const char *wg;
      const char *hy;
      const char *ns;
      const char *hs;
    };
    constexpr SlotName kSlotNames[] = {
        {ParamId::Note, nullptr, nullptr, nullptr, "Master"},
        {ParamId::Velocity, nullptr, nullptr, nullptr, "Slave"},
        {ParamId::Filter, "Bright", nullptr, "Color", nullptr},
        {ParamId::Attack, "Pick", "Detune", "Regen", nullptr},
        {ParamId::Decay, "T60", "Mix", "Chaos", nullptr},
    };
  }

  VoiceParamSet getPresetParamSet(uint8_t presetIndex) noexcept
  {
    if (presetIndex >= getPresetCount())
    {
      return PARAMSET_STANDARD;
    }
    return static_cast<VoiceParamSet>(getPresetConfig(presetIndex).paramSet);
  }

  const char *getSequencerParamName(uint8_t presetIndex, ParamId id) noexcept
  {
    const VoiceParamSet set = getPresetParamSet(presetIndex);
    if (set == PARAMSET_STANDARD)
    {
      return nullptr;
    }
    for (const auto &slot : kSlotNames)
    {
      if (slot.id != id)
      {
        continue;
      }
      switch (set)
      {
      case PARAMSET_WAVEGUIDE:
        return slot.wg;
      case PARAMSET_HYPERSAW:
        return slot.hy;
      case PARAMSET_NOISESTORM:
        return slot.ns;
      case PARAMSET_HARDSYNC:
        return slot.hs;
      default:
        return nullptr;
      }
    }
    return nullptr;
  }

  int presetIndexForPad(uint8_t padIndex, uint8_t presetCount) noexcept
  {
    constexpr uint8_t kBasePad = 8; // first preset pad in settings mode
    if (presetCount == 0 || padIndex < kBasePad)
    {
      return -1;
    }
    const uint8_t idx = static_cast<uint8_t>(padIndex - kBasePad);
    return (idx < presetCount) ? static_cast<int>(idx) : -1;
  }

  const VoiceConfig &getPresetConfig(uint8_t presetIndex) noexcept
  {
    switch (presetIndex)
    {
    default:
    case 0:
      return getAnalogVoice();
    case 1:
      return getDigitalVoice();
    case 2:
      return getBassVoice();
    case 3:
      return getLeadVoice();
    case 4:
      return getSquareVoice();
    case 5:
      return getPadVoice();
    case 6:
      return getPercussionVoice();
    case 7:
      return getSubFunkVoice();
    case 8:
      return getRubberSubVoice();
    case 9:
      return getWaveguidePluckVoice();
    case 10:
      return getWaveguideNylonVoice();
    case 11:
      return getWaveguideBellVoice();
    case 12:
      return getWaveguideShimmerVoice();
    case 13:
      return getHypersawVoice();
    case 14:
      return getNoiseStormVoice();
    }
  }

  uint8_t getPresetCount() noexcept { return VOICE_PRESET_COUNT; }

} // namespace VoicePresets
