// ControlSurfaceLogic.cpp — see ControlSurfaceLogic.h for the contracts.
// Pure C++: no Arduino includes, so the host test suite links this directly.

#include "ControlSurfaceLogic.h"

#include "../voice/ReverbSettings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ControlSurface
{

namespace
{
uint16_t faderDelta(uint16_t a, uint16_t b)
{
  return static_cast<uint16_t>(a > b ? a - b : b - a);
}

uint16_t medianOfThree(uint16_t a, uint16_t b, uint16_t c)
{
  if (a > b)
  {
    const uint16_t swap = a;
    a = b;
    b = swap;
  }
  if (b > c)
  {
    const uint16_t swap = b;
    b = c;
    c = swap;
  }
  if (a > b)
  {
    const uint16_t swap = a;
    a = b;
    b = swap;
  }
  return b;
}
} // namespace

int8_t combineOctaveOffsets(int8_t sequencerOffset,
                            float encoderOffset) noexcept
{
  constexpr int kSemitonesPerOctave = 12;
  constexpr int kMaximumTransposeSemitones = 2 * kSemitonesPerOctave;
  const float clampedEncoderOffset = std::clamp(encoderOffset, -1.0f, 1.0f);
  const int encoderSemitones =
      static_cast<int>(std::lround(clampedEncoderOffset * kSemitonesPerOctave));
  const int combined = static_cast<int>(sequencerOffset) + encoderSemitones;
  return static_cast<int8_t>(std::clamp(combined, -kMaximumTransposeSemitones,
                                        kMaximumTransposeSemitones));
}

// --- ModeStabilizer -----------------------------------------------------------

void ModeStabilizer::begin(Mode initialMode, uint32_t nowMs)
{
  (void)nowMs;
  mode_ = initialMode;
  candidateValid_ = false;
  pendingEdge_ = false;
}

Mode ModeStabilizer::update(bool rawHigh, uint32_t nowMs)
{
  const Mode target = (rawHigh == kModeParamLevel) ? Mode::Param : Mode::Utility;

  if (target == mode_)
  {
    candidateValid_ = false;
    return mode_;
  }

  if (!candidateValid_ || target != candidate_)
  {
    candidate_ = target;
    candidateSinceMs_ = nowMs;
    candidateValid_ = true;
  }

  if (nowMs - candidateSinceMs_ >= kModeStabilityMs)
  {
    mode_ = target;
    candidateValid_ = false;
    pendingEdge_ = true;
  }
  return mode_;
}

// --- PadBank ------------------------------------------------------------------

PadPair PadBank::pairFor(uint8_t selectedVoice)
{
  if (selectedVoice >= 4)
  {
    selectedVoice = 0; // clamp out-of-range selection to voice 1
  }
  const uint8_t low = static_cast<uint8_t>(selectedVoice & ~1u); // pair base
  const uint8_t high = static_cast<uint8_t>(low | 1u);
  return PadPair{low, high};
}

PadAddress PadBank::resolve(uint8_t padIndex, uint8_t selectedVoice)
{
  if (padIndex >= kPadCount)
  {
    padIndex = static_cast<uint8_t>(kPadCount - 1);
  }
  const PadPair pair = pairFor(selectedVoice);
  const bool highBank = (padIndex / kStepsPerVoice) != 0;
  PadAddress addr;
  addr.voice = highBank ? pair.highVoice : pair.lowVoice;
  addr.step = static_cast<uint8_t>(padIndex % kStepsPerVoice);
  return addr;
}

// --- ShiftLatch ---------------------------------------------------------------

void ShiftLatch::reset()
{
  for (uint8_t i = 0; i < kParamCount; ++i)
  {
    momentary_[i] = false;
  }
  latched_ = kNoLatch;
}

void ShiftLatch::onParamButton(uint8_t paramId, bool pressed, bool shiftHeld)
{
  if (paramId >= kParamCount)
  {
    return;
  }

  if (pressed)
  {
    momentary_[paramId] = true;
    if (shiftHeld)
    {
      latched_ = (latched_ == static_cast<int8_t>(paramId)) ? kNoLatch
                                                            : static_cast<int8_t>(paramId);
    }
  }
  else
  {
    momentary_[paramId] = false;
  }
}

void ShiftLatch::applyTo(bool *heldOut, uint8_t count) const
{
  if (heldOut == nullptr)
  {
    return;
  }
  for (uint8_t i = 0; i < count; ++i)
  {
    const bool inRange = i < kParamCount;
    heldOut[i] = inRange && (momentary_[i] || static_cast<int8_t>(i) == latched_);
  }
}

// --- FaderMap -----------------------------------------------------------------

FaderAssignment FaderMap::assignmentFor(bool stepSelected, uint8_t channel)
{
  FaderAssignment out;
  if (channel >= kChannelCount)
  {
    return out;
  }

  if (stepSelected)
  {
    static constexpr ParamId kEnvLanes[kChannelCount] = {
        ParamId::Attack, ParamId::Decay, ParamId::Sustain, ParamId::Release};
    out.target = FaderTarget::EnvLane;
    out.paramId = kEnvLanes[channel];
    return out;
  }

  static constexpr FaderTarget kTargets[kChannelCount] = {
      FaderTarget::Tempo, FaderTarget::DelayMix, FaderTarget::MasterVolume,
      FaderTarget::GateLength};
  out.target = kTargets[channel];
  return out;
}

// Why Arpeggiator mode gets its own table instead of tagging lanes: the four
// faders there are the whole continuous control surface of the arp, and none of
// them means anything in step terms (tempo, swing and gate length all drive the
// step sequencer). The unshifted layer shapes rhythm; Shift swaps to the
// continuous range/gate/swing/filter layer. Values are interpreted by the
// engine, which clamps them.
FaderAssignment FaderMap::arpAssignmentFor(uint8_t channel, bool shift)
{
  FaderAssignment out;
  if (channel >= kChannelCount)
  {
    return out;
  }
  static constexpr FaderTarget kTargets[kChannelCount] = {
      FaderTarget::ArpHits, FaderTarget::ArpLength, FaderTarget::ArpRotate,
      FaderTarget::ArpAccent};
  static constexpr FaderTarget kShiftTargets[kChannelCount] = {
      FaderTarget::ArpOctaves, FaderTarget::ArpGate, FaderTarget::ArpSwing,
      FaderTarget::ArpFilter};
  out.target = shift ? kShiftTargets[channel] : kTargets[channel];
  return out;
}

float FaderMap::normalize(uint16_t rawCounts)
{
  float v = static_cast<float>(rawCounts) / static_cast<float>(kFaderMaxCounts);
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return v;
}

bool FaderMap::accept(uint8_t channel, uint16_t rawCounts)
{
  if (channel >= kChannelCount)
  {
    return false;
  }

  // Keep a rolling median so one noisy tile frame cannot become a control write.
  // A full window is required before a freshly reset channel establishes its
  // pickup baseline; a stable deliberate move normally needs two fresh frames.
  sampleWindow_[channel][sampleCursor_[channel]] = rawCounts;
  sampleCursor_[channel] =
      static_cast<uint8_t>((sampleCursor_[channel] + 1) % kFilterWindowSamples);
  if (sampleCount_[channel] < kFilterWindowSamples)
  {
    ++sampleCount_[channel];
  }
  if (sampleCount_[channel] < kFilterWindowSamples)
  {
    return false;
  }

  const uint16_t value = medianOfThree(
      sampleWindow_[channel][0], sampleWindow_[channel][1],
      sampleWindow_[channel][2]);
  filtered_[channel] = value;

  if (!hasBaseline_[channel])
  {
    baseline_[channel] = value;
    hasBaseline_[channel] = true;
    return false;
  }

  // If not yet engaged, require an obvious filtered move from the baseline.
  if (!engaged_[channel])
  {
    if (faderDelta(baseline_[channel], value) >= kMoveThresholdCounts)
    {
      engaged_[channel] = true;
      lastSent_[channel] = value;
      return true;
    }
    return false;
  }

  // Once engaged, send only when filtered movement meets or exceeds deadband.
  if (faderDelta(lastSent_[channel], value) >= kDeadbandCounts)
  {
    lastSent_[channel] = value;
    return true;
  }
  return false;
}

uint16_t FaderMap::filtered(uint8_t channel) const
{
  return channel < kChannelCount ? filtered_[channel] : 0;
}

void FaderMap::resetDeadband()
{
  for (uint8_t i = 0; i < kChannelCount; ++i)
  {
    sampleCursor_[i] = 0;
    sampleCount_[i] = 0;
    baseline_[i] = 0;
    lastSent_[i] = 0;
    filtered_[i] = 0;
    hasBaseline_[i] = false;
    engaged_[i] = false;
  }
}

void FaderMap::resetChannel(uint8_t channel)
{
  if (channel >= kChannelCount)
  {
    return;
  }
  sampleCursor_[channel] = 0;
  sampleCount_[channel] = 0;
  baseline_[channel] = 0;
  lastSent_[channel] = 0;
  filtered_[channel] = 0;
  hasBaseline_[channel] = false;
  engaged_[channel] = false;
}

bool FaderMap::isEngaged(uint8_t channel) const
{
  return channel < kChannelCount && engaged_[channel];
}

// --- EncoderMotion --------------------------------------------------------------

void EncoderMotion::add(float increment)
{
  if (increment == 0.0f || !std::isfinite(increment))
  {
    return;
  }
  if (pending_ != 0.0f && (increment > 0.0f) != (pending_ > 0.0f))
  {
    pending_ = 0.0f;
  }
  pending_ += increment;
}

float EncoderMotion::takeContinuous(float noiseFloor)
{
  if (std::fabs(pending_) < noiseFloor)
  {
    return 0.0f;
  }
  const float motion = pending_;
  pending_ = 0.0f;
  return motion;
}

int EncoderMotion::takeSteps(float detent)
{
  if (!(detent > 0.0f))
  {
    return 0;
  }
  const int steps = static_cast<int>(pending_ / detent); // truncates toward zero
  pending_ -= static_cast<float>(steps) * detent;
  return steps;
}



// --- Reverb page ----------------------------------------------------------------

namespace
{
struct ReverbCurve
{
  const char *name;
  float low;
  float high;
  bool logarithmic;
};

// One row per ReverbControl (Count excluded). The limits are ReverbParams', so the
// fader, the audio adapter and the session file can never disagree about a range.
constexpr ReverbCurve kReverbCurves[static_cast<size_t>(ReverbControl::Count)] = {
    {"Mix", ReverbParams::kMixMin, ReverbParams::kMixMax, false},
    {"Decay", ReverbParams::kDecayMin, ReverbParams::kDecayMax, true},
    {"Damp", ReverbParams::kDampingMin, ReverbParams::kDampingMax, true},
    {"LowCut", ReverbParams::kLowCutMin, ReverbParams::kLowCutMax, true},
    {"Diffuse", ReverbParams::kDiffusionMin, ReverbParams::kDiffusionMax, false},
    {"Mod", ReverbParams::kModDepthMin, ReverbParams::kModDepthMax, false},
    {"Width", ReverbParams::kWidthMin, ReverbParams::kWidthMax, false},
};

const ReverbCurve *curveFor(ReverbControl control) noexcept
{
  const size_t index = static_cast<size_t>(control);
  return index < static_cast<size_t>(ReverbControl::Count) ? &kReverbCurves[index] : nullptr;
}

// !(x > 0) also catches NaN without a NaN comparison the compiler may drop.
float clampUnit(float value) noexcept
{
  if (!ReverbParams::finite(value) || !(value > 0.0f))
    return 0.0f;
  return value > 1.0f ? 1.0f : value;
}
} // namespace

const char *reverbControlName(ReverbControl control) noexcept
{
  const ReverbCurve *curve = curveFor(control);
  return curve ? curve->name : "";
}

float reverbValueForFader(ReverbControl control, float normalized) noexcept
{
  const ReverbCurve *curve = curveFor(control);
  if (!curve)
    return 0.0f;
  const float n = clampUnit(normalized);
  if (n <= 0.0f)
    return curve->low;
  if (n >= 1.0f)
    return curve->high;
  if (curve->logarithmic)
    return curve->low * std::pow(curve->high / curve->low, n);
  return curve->low + (curve->high - curve->low) * n;
}

float reverbFaderForValue(ReverbControl control, float value) noexcept
{
  const ReverbCurve *curve = curveFor(control);
  if (!curve || !ReverbParams::finite(value))
    return 0.0f;
  if (value <= curve->low)
    return 0.0f;
  if (value >= curve->high)
    return 1.0f;
  if (curve->logarithmic)
    return std::log(value / curve->low) / std::log(curve->high / curve->low);
  return (value - curve->low) / (curve->high - curve->low);
}

float reverbValueOf(const ReverbSettings &settings, ReverbControl control) noexcept
{
  switch (control)
  {
  case ReverbControl::Mix: return settings.mix;
  case ReverbControl::Decay: return settings.decaySeconds;
  case ReverbControl::Damping: return settings.dampingHz;
  case ReverbControl::LowCut: return settings.lowCutHz;
  case ReverbControl::Diffusion: return settings.diffusion;
  case ReverbControl::ModDepth: return settings.modDepth;
  case ReverbControl::Width: return settings.width;
  case ReverbControl::Count: break;
  }
  return 0.0f;
}

void formatReverbValue(ReverbControl control, float value, char *out, size_t size) noexcept
{
  if (!out || size == 0)
    return;
  const ReverbCurve *curve = curveFor(control);
  if (!curve || !ReverbParams::finite(value))
  {
    std::snprintf(out, size, "--");
    return;
  }
  value = std::clamp(value, curve->low, curve->high);
  switch (control)
  {
  case ReverbControl::Decay:
    if (value < 1.0f)
      std::snprintf(out, size, "%.2fs", static_cast<double>(value));
    else if (value < 10.0f)
      std::snprintf(out, size, "%.1fs", static_cast<double>(value));
    else
      std::snprintf(out, size, "%.0fs", static_cast<double>(value));
    break;
  case ReverbControl::Damping:
    if (value >= 1000.0f)
      std::snprintf(out, size, "%.1fkHz", static_cast<double>(value) / 1000.0);
    else
      std::snprintf(out, size, "%.0fHz", static_cast<double>(value));
    break;
  case ReverbControl::LowCut:
    std::snprintf(out, size, "%.0fHz", static_cast<double>(value));
    break;
  default: // Mix, Diffusion, Modulation depth, Width: percent of the natural setting
    std::snprintf(out, size, "%ld%%", std::lround(static_cast<double>(value) * 100.0));
    break;
  }
}

const char *loopControlName(LoopControl control) noexcept
{
  switch (control)
  {
  case LoopControl::LoopVolume: return "Loop Vol";
  case LoopControl::LoopLength: return "Length";
  case LoopControl::SequencerVolume: return "Seq Vol";
  case LoopControl::Regen: return "Regen";
  case LoopControl::Count: break;
  }
  return "";
}

void formatLoopValue(LoopControl control, float value, char *out, size_t size) noexcept
{
  if (!out || size == 0)
    return;
  if (control == LoopControl::Count || !(value == value))
  {
    std::snprintf(out, size, "--");
    return;
  }
  if (control == LoopControl::LoopLength)
    std::snprintf(out, size, "%u st", static_cast<unsigned>(std::lround(static_cast<double>(value))));
  else
    std::snprintf(out, size, "%ld%%", std::lround(static_cast<double>(std::clamp(value, 0.0f, 1.0f)) * 100.0));
}

} // namespace ControlSurface
