#include "SitarParameters.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// SitarParameters.cpp — normalized/engineering mapping and value text for the
// sitar.h lane table (portable: no Arduino, no rpdsp).

namespace Sitar
{
namespace
{
// A lane whose range starts at or below zero cannot be mapped logarithmically,
// whatever the table asks for.
bool usesExponential(const ParamInfo &info) noexcept
{
    return info.exponential && info.min > 0.0f && info.max > info.min;
}

float clamp01(float value) noexcept
{
    if (!(value > 0.0f)) // also catches NaN
        return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

float clampValue(const ParamInfo &info, float value) noexcept
{
    if (!(value == value)) // NaN
        return info.min;
    if (value < info.min)
        return info.min;
    return value > info.max ? info.max : value;
}
} // namespace

float engineeringValue(Param id, float normalized) noexcept
{
    const ParamInfo &info = paramInfo(id);
    const float travel = clamp01(normalized);
    if (usesExponential(info))
        return info.min * std::pow(info.max / info.min, travel);
    return info.min + (info.max - info.min) * travel;
}

float normalizedValue(Param id, float engineering) noexcept
{
    const ParamInfo &info = paramInfo(id);
    const float value = clampValue(info, engineering);
    if (usesExponential(info))
    {
        const float ratio = value / info.min;
        if (!(ratio > 0.0f))
            return 0.0f;
        return clamp01(std::log(ratio) / std::log(info.max / info.min));
    }
    return clamp01((value - info.min) / (info.max - info.min));
}

void formatEngineeringValue(Param id, float engineering, char *out, size_t outSize) noexcept
{
    if (out == nullptr || outSize == 0)
        return;
    const ParamInfo &info = paramInfo(id);
    const float value = clampValue(info, engineering);
    if (info.unit[0] == '\0')
        std::snprintf(out, outSize, "%.2f", static_cast<double>(value));
    else
        std::snprintf(out, outSize, "%.2f %s", static_cast<double>(value), info.unit);
}

void formatParamValue(Param id, float normalized, char *out, size_t outSize) noexcept
{
    formatEngineeringValue(id, engineeringValue(id, normalized), out, outSize);
}

const char *groupName(ParamGroup group) noexcept
{
    switch (group)
    {
    case ParamGroup::String: return "String";
    case ParamGroup::Jawari: return "Jawari";
    case ParamGroup::Taraf: return "Taraf";
    case ParamGroup::Body: return "Body";
    case ParamGroup::Meend: return "Meend";
    case ParamGroup::Count: break;
    }
    return "?";
}

void formatParamPosition(Param id, char *out, size_t outSize) noexcept
{
    if (out == nullptr || outSize == 0)
        return;
    std::snprintf(out, outSize, "%u/%u", static_cast<unsigned>(paramIndex(id) + 1),
                  static_cast<unsigned>(kParamCount));
}

} // namespace Sitar
