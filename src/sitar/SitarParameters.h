#pragma once

#include <cstddef>
#include <cstdint>

// SitarParameters.h — every control `rpdsp::SitarStringVoice` exposes, as one
// browsable table (portable, Core 0).
// Musical role: this is what Sitar Explorer exists for — the performer steps
// through all thirteen setters of sitar.h in a fixed order and hears each one,
// instead of guessing which of them the firmware happened to wire up.
// Technical role: the single source of truth for each lane's name, group,
// range, default and normalized<->engineering mapping, shared by the encoder,
// the faders, the focused lane, the OLED page, the LED palette and the console
// dump. The DSP binding lives in SitarInstrument.cpp (applyParameter) so this
// header stays hardware- and rpdsp-free and stays host-testable.
//
// Every default here equals the corresponding member initializer in
// src/rpdsp/src/rpdsp/sitar.h — test_sitar.cpp proves it by rendering an
// untouched voice and a defaults-applied voice and comparing them sample for
// sample.
namespace Sitar
{

enum class Param : uint8_t
{
    StringT60 = 0,  // setDecayTimeSeconds
    Brightness,     // setBrightness
    PickPosition,   // setPickPosition
    PickHardness,   // setPickHardness
    Stiffness,      // setStiffness
    Detune,         // setDetuneCents
    Jawari,         // setJawari
    JawariContact,  // setJawariThreshold
    TarafAmount,    // setTarafAmount
    TarafRing,      // setTarafDecaySeconds
    BodyAmount,     // setBodyAmount
    BodyTone,       // setBodyFrequency
    Meend,          // setSlideTimeSeconds
    Count
};

constexpr uint8_t kParamCount = static_cast<uint8_t>(Param::Count);

// Browsing groups: five families the performer can jump to (explore-row pads),
// so the encoder cursor is never more than a few detents from anything.
enum class ParamGroup : uint8_t
{
    String = 0,
    Jawari,
    Taraf,
    Body,
    Meend,
    Count
};

constexpr uint8_t kGroupCount = static_cast<uint8_t>(ParamGroup::Count);

struct ParamInfo
{
    const char *name;     // Sitar vocabulary shown on the OLED
    const char *unit;     // "s", "Hz", "ct" or ""
    const char *note;     // One line: what the performer hears change
    const char *setter;   // The sitar.h setter this lane drives
    ParamGroup group;
    float min;
    float max;
    float defaultValue;   // Engineering units, equal to sitar.h's default
    // The slice a sitarist would actually set: the randomize pad stays inside
    // these so it never produces a sitar nobody would play (silent taraf,
    // ten-second strings, a body tuned to a rumble).
    float musicalMin;
    float musicalMax;
    // Ranges that are heard logarithmically (times, Hz) travel exponentially
    // under the faders, so the bottom of the travel is not a dead zone.
    bool exponential;
};

constexpr uint8_t paramIndex(Param id)
{
    return static_cast<uint8_t>(id);
}

// Table order is the encoder's browse order and mirrors the sitar.h header.
inline constexpr ParamInfo kParamTable[kParamCount] = {
    {"Ring (T60)", "s", "How long the string rings", "setDecayTimeSeconds",
     ParamGroup::String, 0.05f, 10.0f, 5.0f, 2.0f, 8.0f, true},
    {"Brightness", "", "Loop damping: dark to brilliant", "setBrightness",
     ParamGroup::String, 0.0f, 1.0f, 0.92f, 0.62f, 0.99f, false},
    {"Mizrab point", "", "Where the pick meets the string", "setPickPosition",
     ParamGroup::String, 0.02f, 0.5f, 0.12f, 0.05f, 0.24f, false},
    {"Mizrab hardness", "", "Soft felt to hard wire pick", "setPickHardness",
     ParamGroup::String, 0.0f, 1.0f, 0.9f, 0.45f, 1.0f, false},
    {"Stiffness", "", "Upper-partial dispersion", "setStiffness",
     ParamGroup::String, 0.0f, 1.0f, 0.15f, 0.0f, 0.35f, false},
    {"Coupling", "ct", "Spread between the two strings", "setDetuneCents",
     ParamGroup::String, 0.0f, 30.0f, 3.0f, 0.0f, 8.0f, false},
    {"Jawari buzz", "", "Bridge buzz the string can touch", "setJawari",
     ParamGroup::Jawari, 0.0f, 1.0f, 0.45f, 0.15f, 0.9f, false},
    {"Bridge contact", "", "How easily the string touches", "setJawariThreshold",
     ParamGroup::Jawari, 0.0f, 1.0f, 0.3f, 0.12f, 0.5f, false},
    {"Taraf level", "", "Sympathetic strings in the mix", "setTarafAmount",
     ParamGroup::Taraf, 0.0f, 1.0f, 0.35f, 0.1f, 0.6f, false},
    {"Taraf ring", "s", "How long the taraf keeps singing", "setTarafDecaySeconds",
     ParamGroup::Taraf, 0.05f, 12.0f, 4.0f, 1.5f, 8.0f, true},
    {"Body level", "", "Acoustic body in the mix", "setBodyAmount",
     ParamGroup::Body, 0.0f, 1.0f, 0.25f, 0.08f, 0.5f, false},
    {"Body tone", "Hz", "Low body resonance", "setBodyFrequency",
     ParamGroup::Body, 50.0f, 500.0f, 130.0f, 90.0f, 200.0f, true},
    {"Meend time", "s", "Bend time of a slide", "setSlideTimeSeconds",
     ParamGroup::Meend, 0.005f, 2.0f, 0.12f, 0.06f, 0.35f, true},
};

// Clamped lookup: an out-of-range id reads lane 0 instead of past the table.
inline constexpr const ParamInfo &paramInfo(Param id)
{
    return kParamTable[paramIndex(id) < kParamCount ? paramIndex(id) : 0];
}

// Group a lane belongs to.
constexpr ParamGroup groupOf(Param id)
{
    return paramInfo(id).group;
}

// First lane of a group, i.e. where a group-jump pad lands the cursor.
constexpr Param firstParamOf(ParamGroup wanted)
{
    for (uint8_t index = 0; index < kParamCount; ++index)
    {
        if (kParamTable[index].group == wanted)
            return static_cast<Param>(index);
    }
    return Param::StringT60;
}

// Next lane in browse order, wrapping at the end.
constexpr Param nextParam(Param id)
{
    return static_cast<Param>((paramIndex(id) + 1) % kParamCount);
}

// Previous lane in browse order, wrapping at the start.
constexpr Param previousParam(Param id)
{
    return static_cast<Param>((paramIndex(id) + kParamCount - 1) % kParamCount);
}

// Normalized 0..1 (fader/encoder travel) -> engineering value of the lane.
float engineeringValue(Param id, float normalized) noexcept;

// Engineering value -> normalized travel (inverse of the above).
float normalizedValue(Param id, float engineering) noexcept;

// "0.45", "5.0 s", "130 Hz" — the engineering value with its unit, for the
// OLED and the console dump. out is always NUL-terminated.
void formatParamValue(Param id, float normalized, char *out, size_t outSize) noexcept;

// Same, from an engineering value the caller already holds.
void formatEngineeringValue(Param id, float engineering, char *out, size_t outSize) noexcept;

const char *groupName(ParamGroup group) noexcept;

// "3/13" — where the cursor sits in the browse order, so the performer can see
// that there is more to explore in a given direction.
void formatParamPosition(Param id, char *out, size_t outSize) noexcept;

} // namespace Sitar
