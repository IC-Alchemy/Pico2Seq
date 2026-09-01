// ControlSurfaceLogic.cpp — see ControlSurfaceLogic.h for the contracts.
// Pure C++: no Arduino includes, so the host test suite links this directly.

#include "ControlSurfaceLogic.h"

namespace ControlSurface
{

namespace
{
uint16_t faderDelta(uint16_t a, uint16_t b)
{
  return static_cast<uint16_t>(a > b ? a - b : b - a);
}
} // namespace

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

FaderAssignment FaderMap::assignmentFor(Mode mode, uint8_t channel)
{
  FaderAssignment out;
  if (channel >= kChannelCount)
  {
    out.target = FaderTarget::StepParam;
    out.paramId = ParamId::Count;
    return out;
  }

  if (mode == Mode::Param)
  {
    // Design table: Filter, Attack, Decay, Velocity for the selected voice.
    static constexpr ParamId kParamModeParams[kChannelCount] = {
        ParamId::Filter, ParamId::Attack, ParamId::Decay, ParamId::Velocity};
    out.target = FaderTarget::StepParam;
    out.paramId = kParamModeParams[channel];
    return out;
  }

  static constexpr FaderTarget kUtilityModeTargets[kChannelCount] = {
      FaderTarget::Tempo, FaderTarget::SwingAmount, FaderTarget::DelayMix,
      FaderTarget::GateLength};
  out.target = kUtilityModeTargets[channel];
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
  const bool send = !valid_[channel] ||
                    faderDelta(lastSent_[channel], rawCounts) >= kDeadbandCounts;
  if (send)
  {
    lastSent_[channel] = rawCounts;
    valid_[channel] = true;
  }
  return send;
}

void FaderMap::resetDeadband()
{
  for (uint8_t i = 0; i < kChannelCount; ++i)
  {
    valid_[i] = false;
  }
}

} // namespace ControlSurface
