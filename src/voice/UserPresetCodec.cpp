// UserPresetCodec.cpp - validation, canonicalisation and the record <-> voice bridge.
#include "UserPresetCodec.h"

#include "PatchCodec.h"
#include "PatchFields.h"
#include "VoiceConfig.h"
#include "VoiceEditParameters.h"
#include "VoicePresets.h"
#include <cmath>
#include <cstring>

namespace usercodec
{
namespace
{
using persistence::PatchSnapshot;
using persistence::UserPresetRecord;

constexpr uint8_t kUsePatchBases = 1u << 0;

bool validName(const char (&name)[persistence::kUserPresetNameSize]) noexcept
{
    size_t length = 0;
    while (length < sizeof name && name[length] != '\0')
    {
        const unsigned char c = static_cast<unsigned char>(name[length]);
        if (c < 0x20 || c > 0x7E)
            return false;
        ++length;
    }
    if (length == 0 || length >= sizeof name)
        return false;
    bool visible = false;
    for (size_t i = 0; i < length; ++i)
        visible = visible || name[i] != ' ';
    if (!visible)
        return false;
    for (size_t i = length; i < sizeof name; ++i)
        if (name[i] != '\0')
            return false; // junk after the terminator would make equal names compare unequal
    return true;
}

bool validWaveform(uint8_t wave) noexcept
{
    return wave <= WAVE_HARDSYNC_SAW || wave == WAVE_NOISE;
}

// Choice-type rows: the value must be one the engine knows how to play.
bool validChoice(const patchfields::Field &f, float v) noexcept
{
    const uint8_t b = static_cast<uint8_t>(v);
    switch (f.edit)
    {
    case VoiceEdit::Id::Engine: return b <= ENGINE_RECIPE;
    case VoiceEdit::Id::OscCount: return b <= 3;
    case VoiceEdit::Id::FilterType: return b <= FILTER_SVF;
    case VoiceEdit::Id::FilterMode: return b <= static_cast<uint8_t>(VoiceFilterMode::HP12);
    case VoiceEdit::Id::Wave1:
    case VoiceEdit::Id::Wave2:
    case VoiceEdit::Id::Wave3: return validWaveform(b);
    default: return true;
    }
}

uint8_t derivedParamSet(const UserPresetRecord &r, uint8_t baseParamSet) noexcept
{
    switch (r.patch.engine)
    {
    case ENGINE_WAVEGUIDE: return PARAMSET_WAVEGUIDE;
    case ENGINE_HYPERSAW: return PARAMSET_HYPERSAW;
    case ENGINE_NOISEFX: return PARAMSET_NOISESTORM;
    case ENGINE_RECIPE: return baseParamSet;
    default: break;
    }
    const uint8_t active = r.patch.oscillatorCount < 3 ? r.patch.oscillatorCount : 3;
    for (uint8_t i = 0; i < active; ++i)
        if (r.patch.oscWaveforms[i] == WAVE_HARDSYNC_SAW)
            return PARAMSET_HARDSYNC;
    return PARAMSET_STANDARD;
}
} // namespace

const char *problemName(Problem problem) noexcept
{
    switch (problem)
    {
    case Problem::None: return "ok";
    case Problem::BadName: return "bad name";
    case Problem::BadPlace: return "bad page or pad";
    case Problem::BadBase: return "unknown base preset";
    case Problem::BadReserved: return "reserved bytes set";
    case Problem::BadField: return "value out of range";
    case Problem::EngineNeedsRecipe: return "recipe engine needs a recipe base preset";
    }
    return "invalid";
}

void canonicalize(UserPresetRecord &r) noexcept
{
    bool seenTerminator = false;
    for (char &c : r.name)
    {
        seenTerminator = seenTerminator || c == '\0';
        if (seenTerminator)
            c = '\0';
    }
    r.name[persistence::kUserPresetNameSize - 1] = '\0';
    r.flags = 0;
    r.reserved = 0;
    r.patch.reserved[0] = r.patch.reserved[1] = 0;
    r.patch.presetIndex = r.baseIndex;
    r.patch.flags = static_cast<uint8_t>(r.patch.flags | kUsePatchBases);

    // Quantised bases: the on-device editor stores whole scale steps and whole octaves.
    if (patchfields::finiteFloat(r.patch.baseNote))
        r.patch.baseNote = std::round(r.patch.baseNote);
    if (patchfields::finiteFloat(r.patch.baseOctave))
        r.patch.baseOctave = std::round(r.patch.baseOctave / 12.0f) * 12.0f;

    uint8_t baseParamSet = PARAMSET_STANDARD;
    if (r.baseIndex < VoicePresets::getPresetCount())
        baseParamSet = VoicePresets::getPresetConfig(r.baseIndex).paramSet;
    r.patch.paramSet = derivedParamSet(r, baseParamSet);
}

bool toConfig(const UserPresetRecord &r, VoiceConfig &out) noexcept
{
    PatchSnapshot patch = r.patch;
    patch.flags = static_cast<uint8_t>(patch.flags | kUsePatchBases);
    if (!voicecodec::applyPatch(r.baseIndex, patch, out))
    {
        if (r.baseIndex >= VoicePresets::getPresetCount())
            out = VoicePresets::getPresetConfig(0);
        return false;
    }
    out.usePatchBases = true;
    return true;
}

void fromConfig(const VoiceConfig &config, uint8_t baseIndex, UserPresetRecord &out) noexcept
{
    std::memset(&out, 0, sizeof out);
    voicecodec::capturePatch(config, out.patch);
    out.baseIndex = baseIndex;
    out.patch.presetIndex = baseIndex;
    out.patch.paramSet = config.paramSet; // keep the running voice's own lane meaning
    out.patch.flags = static_cast<uint8_t>(out.patch.flags | kUsePatchBases);
    out.page = persistence::kFirstUserPage;
    out.colorR = out.colorG = out.colorB = 0xFF;
}

void fromFactory(uint8_t presetIndex, UserPresetRecord &out) noexcept
{
    const uint8_t index = presetIndex < VoicePresets::getPresetCount() ? presetIndex : 0;
    fromConfig(VoicePresets::getPresetConfig(index), index, out);
    std::strncpy(out.name, VoicePresets::getPresetName(index), persistence::kUserPresetNameSize - 1);
    canonicalize(out);
}

Check validate(const UserPresetRecord &r) noexcept
{
    Check result;
    if (!validName(r.name))
        return {Problem::BadName, 0xFF};
    if (!persistence::validUserPlace(r.page, r.pad))
        return {Problem::BadPlace, 0xFF};
    if (r.baseIndex >= VoicePresets::getPresetCount() || r.patch.presetIndex != r.baseIndex)
        return {Problem::BadBase, 0xFF};
    if (r.flags != 0 || r.reserved != 0 || r.patch.reserved[0] != 0 || r.patch.reserved[1] != 0)
        return {Problem::BadReserved, 0xFF};

    // Choices and counts first: they decide which layout the remaining ranges come from.
    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const patchfields::Field &f = patchfields::field(i);
        if (f.type == patchfields::Type::Float)
        {
            if (!patchfields::finiteFloat(patchfields::read(r.patch, f)))
                return {Problem::BadField, static_cast<uint8_t>(i)};
        }
        else if (f.type == patchfields::Type::Byte &&
                 !validChoice(f, patchfields::read(r.patch, f)))
        {
            return {Problem::BadField, static_cast<uint8_t>(i)};
        }
    }
    if ((r.patch.flags & kUsePatchBases) == 0)
        return {Problem::BadReserved, 0xFF};

    VoiceConfig config;
    if (!toConfig(r, config))
        return {Problem::EngineNeedsRecipe, 0xFF};

    for (size_t i = 0; i < patchfields::count(); ++i)
    {
        const patchfields::Field &f = patchfields::field(i);
        if (f.type == patchfields::Type::Flag)
            continue;
        const float v = patchfields::read(r.patch, f);
        float lo, hi;
        patchfields::limits(f, config, lo, hi);
        // Waveform ids are not a continuous range (noise is 255); validChoice covered them.
        const bool isWave = f.edit == VoiceEdit::Id::Wave1 || f.edit == VoiceEdit::Id::Wave2 ||
                            f.edit == VoiceEdit::Id::Wave3;
        if (isWave)
            continue;
        if (v < lo - kLimitSlack || v > hi + kLimitSlack)
            return {Problem::BadField, static_cast<uint8_t>(i)};
    }
    return result;
}

} // namespace usercodec
