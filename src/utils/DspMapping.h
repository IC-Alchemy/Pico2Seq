#pragma once

#include <cmath>

// Local replacement for the rpdsp fmap/Mapping helpers that were dropped in
// the rpdsp submodule update (b4443b8). The implementation is carried over
// unchanged from rpdsp 7ba95b0, which ported it from the DaisySP core
// (Electrosmith, MIT) so mapped parameter ranges keep their exact previous
// behavior. If rpdsp reintroduces an equivalent, prefer theirs and delete this.

namespace dspmap
{

// Response curves for fmap.
enum class Mapping
{
  LINEAR,
  EXP,
  LOG,
};

// Maps a normalized [0, 1] control value onto [min, max] with the chosen
// response curve. EXP is a square curve (min + in^2 * (max - min)); LOG is
// logarithmic in the sense of DaisySP's fmap (min * 10^(in / a), where a makes
// in == 1 land on max) and requires min, max > 0.
inline float fmap(float in, float min, float max, Mapping curve = Mapping::LINEAR)
{
  auto clampf = [](float value, float low, float high)
  { return value < low ? low : (value > high ? high : value); };
  switch (curve)
  {
  case Mapping::EXP:
    return clampf(min + (in * in) * (max - min), min, max);
  case Mapping::LOG:
  {
    const float a = 1.0f / std::log10(max / min);
    return clampf(min * std::pow(10.0f, in / a), min, max);
  }
  case Mapping::LINEAR:
  default:
    return clampf(min + in * (max - min), min, max);
  }
}

} // namespace dspmap
