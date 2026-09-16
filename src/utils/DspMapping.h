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
  OCT,
};

// Maps a normalized [0, 1] control value onto [min, max] with the chosen
// response curve. EXP is a square curve (min + in^2 * (max - min)); LOG is
// logarithmic in the sense of DaisySP's fmap (min * 10^(in / a), where a makes
// in == 1 land on max) and requires min, max > 0. OCT is the true exponential
// taper min * (max / min)^in: equal travel covers equal octaves (or equal
// time ratios), and it requires min > 0. OCT and LOG are the same curve; new
// tables use OCT because the name says what the taper does.
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
  case Mapping::OCT:
    return clampf(min * std::pow(max / min, in), min, max);
  case Mapping::LINEAR:
  default:
    return clampf(min + in * (max - min), min, max);
  }
}

namespace detail
{
// One half of a centered map: x in [0, 1] runs from a to b along the curve.
inline float halfMap(float x, float a, float b, Mapping curve)
{
  switch (curve)
  {
  case Mapping::LOG:
  case Mapping::OCT:
    return a * std::pow(b / a, x);
  case Mapping::EXP:
    return a + (x * x) * (b - a);
  case Mapping::LINEAR:
  default:
    return a + x * (b - a);
  }
}

// Inverse of halfMap for v in [a, b]. A zero-width half has no travel.
inline float halfNormalize(float v, float a, float b, Mapping curve)
{
  if (b <= a)
    return 0.0f;
  switch (curve)
  {
  case Mapping::LOG:
  case Mapping::OCT:
    return std::log(v / a) / std::log(b / a);
  case Mapping::EXP:
  {
    const float t = (v - a) / (b - a);
    return std::sqrt(t < 0.0f ? 0.0f : t);
  }
  case Mapping::LINEAR:
  default:
    return (v - a) / (b - a);
  }
}
} // namespace detail

// Piecewise map whose midpoint lands on center: [0, 0.5] covers min..center
// and [0.5, 1] covers center..max, each half along the chosen curve. This is
// JUCE's setSkewForCentre idea — a control's middle rests on the sweet spot
// rather than on the arithmetic midpoint of its range.
inline float fmapCentered(float in, float min, float max, float center, Mapping curve)
{
  in = in < 0.0f ? 0.0f : (in > 1.0f ? 1.0f : in);
  return in < 0.5f ? detail::halfMap(in * 2.0f, min, center, curve)
                   : detail::halfMap((in - 0.5f) * 2.0f, center, max, curve);
}

// Inverse of fmapCentered. Values outside [min, max] clamp to the ends.
inline float normalizeCentered(float value, float min, float max, float center, Mapping curve)
{
  value = value < min ? min : (value > max ? max : value);
  return value < center ? 0.5f * detail::halfNormalize(value, min, center, curve)
                        : 0.5f + 0.5f * detail::halfNormalize(value, center, max, curve);
}

} // namespace dspmap
