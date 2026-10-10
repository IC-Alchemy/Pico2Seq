// PatchFields.cpp - the table. Row order is the editor's display order within a group.
#include "PatchFields.h"

#include "PatchCodec.h"
#include "VoiceConfig.h"
#include "engines/RecipeSources.h"
#include <cmath>
#include <cstring>
#include <iterator>

namespace patchfields
{
namespace
{
using persistence::PatchSnapshot;
using Id = VoiceEdit::Id;

#define P_OFF(member) static_cast<uint16_t>(offsetof(PatchSnapshot, member))
#define P_ELEM(member, i) static_cast<uint16_t>(offsetof(PatchSnapshot, member) + (i) * 4)
#define F_FLOAT(key, member, id, show) {key, P_OFF(member), Type::Float, 0, Id::id, Show::show}
#define F_ELEM(key, member, i, id, show) {key, P_ELEM(member, i), Type::Float, 0, Id::id, Show::show}
#define F_INT(key, member, i, id, show) {key, P_ELEM(member, i), Type::Int32, 0, Id::id, Show::show}
#define F_BYTE(key, member, id, show) {key, P_OFF(member), Type::Byte, 0, Id::id, Show::show}
#define F_WAVE(key, i, id, show) \
    {key, static_cast<uint16_t>(offsetof(PatchSnapshot, oscWaveforms) + (i)), Type::Byte, 0, Id::id, Show::show}
// `mask` is a voicecodec flag constant, so this table and the song codec share one definition.
#define F_FLAG(key, mask, id, show) {key, P_OFF(flags), Type::Flag, mask, Id::id, Show::show}

// clang-format off
const Field kFields[] = {
    // Sequenced bases: the patch values the sequencer lanes offset from.
    F_FLOAT("base.note",       baseNote,       Note,       Always),
    F_FLOAT("base.velocity",   baseVelocity,   Velocity,   Always),
    F_FLOAT("base.octave",     baseOctave,     Octave,     Always),
    F_FLOAT("base.gateLength", baseGateLength, GateLength, Always),
    F_FLAG ("base.gate",            voicecodec::kBaseGate,        Gate,            Always),
    F_FLAG ("base.slide",           voicecodec::kBaseSlide,       Slide,           Always),
    F_FLOAT("base.glide",      slideSeconds,   SlideTime,  Always),
    // Source
    F_BYTE ("source.engine",   engine,         Engine,     Always),
    F_BYTE ("source.oscCount", oscillatorCount, OscCount,  EngineOsc),
    // Oscillators
    F_WAVE ("osc1.wave", 0, Wave1, Osc1),
    F_ELEM ("osc1.level",   oscAmplitudes, 0, Level1,   Osc1),
    F_ELEM ("osc1.detune",  oscDetuning,   0, Detune1,  Osc1),
    F_ELEM ("osc1.pulse",   oscPulseWidth, 0, Pulse1,   Pulse1),
    F_INT  ("osc1.harmony", harmony,       0, Harmony1, Harmony1),
    F_WAVE ("osc2.wave", 1, Wave2, Osc2),
    F_ELEM ("osc2.level",   oscAmplitudes, 1, Level2,   Osc2),
    F_ELEM ("osc2.detune",  oscDetuning,   1, Detune2,  Osc2),
    F_ELEM ("osc2.pulse",   oscPulseWidth, 1, Pulse2,   Pulse2),
    F_INT  ("osc2.harmony", harmony,       1, Harmony2, Osc2),
    F_WAVE ("osc3.wave", 2, Wave3, Osc3),
    F_ELEM ("osc3.level",   oscAmplitudes, 2, Level3,   Osc3),
    F_ELEM ("osc3.detune",  oscDetuning,   2, Detune3,  Osc3),
    F_ELEM ("osc3.pulse",   oscPulseWidth, 2, Pulse3,   Pulse3),
    F_INT  ("osc3.harmony", harmony,       2, Harmony3, Osc3),
    // Envelope
    F_FLAG ("env.enabled",          voicecodec::kHasEnvelope,     EnvelopeOn,      Always),
    F_FLOAT("env.attack",   defaultAttack,  EnvAttack,  Envelope),
    F_FLOAT("env.decay",    defaultDecay,   EnvDecay,   Envelope),
    F_FLOAT("env.sustain",  defaultSustain, Sustain,    Envelope),
    F_FLOAT("env.release",  defaultRelease, Release,    Envelope),
    // Main filter
    F_FLAG ("filter.enabled",       voicecodec::kHasFilter,       FilterOn,        Always),
    F_BYTE ("filter.type",      filterType,         FilterType,      Filter),
    F_BYTE ("filter.mode",      filterMode,         FilterMode,      Filter),
    F_FLOAT("filter.cutoff",    filterCutoffBase,   StaticCutoff,    Filter),
    F_FLOAT("filter.resonance", filterRes,          Resonance,       Filter),
    F_FLOAT("filter.drive",     filterDrive,        FilterDrive,     Ladder),
    F_FLOAT("filter.passband",  filterPassbandGain, Passband,        Ladder),
    F_FLOAT("filter.envOctaves", filterEnvelopeOctaves, FilterEnvAmount, Filter),
    F_FLOAT("filter.envRest",   filterEnvelopeRest, FilterEnvFloor,  Filter),
    // High-pass
    {"hp.cutoff", P_OFF(highPassFreq), Type::Float, 0, Id::HighPassFreq, Show::Always, 0.0f, kCatalog},
    F_FLOAT("hp.resonance", highPassRes,  HighPassRes,  Always),
    // Overdrive
    F_FLAG ("drive.enabled",        voicecodec::kHasOverdrive,    DriveOn,         Always),
    F_FLOAT("drive.amount",  overdriveDrive, Drive,     Overdrive),
    F_FLOAT("drive.gain",    overdriveGain,  DriveGain, Overdrive),
    // Plucked string
    F_FLOAT("string.t60",         wgT60,           T60,          EngineWaveguide),
    F_FLOAT("string.brightness",  wgBrightness,    Brightness,   EngineWaveguide),
    F_FLOAT("string.pickPosition", wgPickPosition, PickPosition, EngineWaveguide),
    F_FLOAT("string.pickHardness", wgPickHardness, PickHardness, EngineWaveguide),
    F_FLOAT("string.stiffness",   wgStiffness,     Stiffness,    EngineWaveguide),
    F_FLOAT("string.detune",      wgDetune,        StringDetune, EngineWaveguide),
    // Hypersaw
    F_FLOAT("saw.detune", hypersawDetune, SawDetune, EngineHypersaw),
    F_FLOAT("saw.mix",    hypersawMix,    SawMix,    EngineHypersaw),
    // Noise
    F_FLOAT("noise.level",       noiseSourceLevel, NoiseLevel,   EngineNoiseFx),
    F_FLOAT("noise.chaosRate",   noiseChaosRate,   ChaosRate,    EngineNoiseFx),
    F_FLOAT("noise.diffuseSize", noiseDiffuseSize, DiffuseSize,  EngineNoiseFx),
    F_FLOAT("noise.diffuseMix",  noiseDiffuseMix,  DiffuseMix,   EngineNoiseFx),
    F_FLOAT("noise.swarmColor",  noiseSwarmColor,  SwarmColor,   EngineNoiseFx),
    F_FLOAT("noise.swarmRegen",  noiseSwarmRegen,  SwarmRegen,   EngineNoiseFx),
    F_FLOAT("noise.chaosLevel",  noiseChaosLevel,  ChaosLevel,   EngineNoiseFx),
    // Recipe engine
    F_FLOAT("recipe.macro1",    macro1,            Macro1,          EngineRecipe),
    F_FLOAT("recipe.macro2",    macro2,            Macro2,          EngineRecipe),
    F_FLOAT("recipe.macro3",    macro3,            Macro3,          EngineRecipe),
    F_FLAG ("recipe.retrigger",     voicecodec::kRecipeRetrigger, RecipeRetrigger, EngineRecipe),
    F_FLOAT("recipe.fmFeedback", fmModFeedback,    FmModFeedback,   RecipeFm),
    F_FLOAT("recipe.phaseFold",  phaseTriangleFold, PhaseFold,      RecipePhase),
    F_FLOAT("recipe.subRatio",   spectralSubRatio, SubRatio,        RecipeSpectral),
    F_FLOAT("recipe.subShape",   spectralSubShape, SubShape,        RecipeSpectral),
    F_FLOAT("recipe.driftChaos", prismDriftChaos,  DriftChaos,      RecipePrism),
    // Output
    F_FLOAT("out.level",   outputLevel, Output,  Always),
    F_FLAG ("out.enabled",          voicecodec::kEnabled,         Enabled,         Always),
};
// clang-format on

#undef P_OFF
#undef P_ELEM
#undef F_FLOAT
#undef F_ELEM
#undef F_INT
#undef F_BYTE
#undef F_WAVE
#undef F_FLAG

constexpr size_t kFieldCount = std::size(kFields);
}  // namespace

size_t count() noexcept { return kFieldCount; }

const Field &field(size_t index) noexcept { return kFields[index < kFieldCount ? index : 0]; }

int indexOfKey(const char *key) noexcept
{
    if (!key)
        return -1;
    for (size_t i = 0; i < kFieldCount; ++i)
        if (std::strcmp(kFields[i].key, key) == 0)
            return static_cast<int>(i);
    return -1;
}

float read(const PatchSnapshot &patch, const Field &f) noexcept
{
    const auto *base = reinterpret_cast<const uint8_t *>(&patch);
    switch (f.type)
    {
    case Type::Float:
    {
        float v;
        std::memcpy(&v, base + f.offset, sizeof v);
        return v;
    }
    case Type::Int32:
    {
        int32_t v;
        std::memcpy(&v, base + f.offset, sizeof v);
        return static_cast<float>(v);
    }
    case Type::Byte:
        return static_cast<float>(base[f.offset]);
    case Type::Flag:
        return (base[f.offset] & f.mask) ? 1.0f : 0.0f;
    }
    return 0.0f;
}

void write(PatchSnapshot &patch, const Field &f, float value) noexcept
{
    auto *base = reinterpret_cast<uint8_t *>(&patch);
    switch (f.type)
    {
    case Type::Float:
        std::memcpy(base + f.offset, &value, sizeof value);
        break;
    case Type::Int32:
    {
        const int32_t v = static_cast<int32_t>(std::lround(value));
        std::memcpy(base + f.offset, &v, sizeof v);
        break;
    }
    case Type::Byte:
        base[f.offset] = static_cast<uint8_t>(std::lround(value));
        break;
    case Type::Flag:
        if (value >= 0.5f)
            base[f.offset] = static_cast<uint8_t>(base[f.offset] | f.mask);
        else
            base[f.offset] = static_cast<uint8_t>(base[f.offset] & ~f.mask);
        break;
    }
}

void limits(const Field &f, const VoiceConfig &config, float &minimum, float &maximum) noexcept
{
    VoiceEdit::limits(f.edit, config, minimum, maximum);
    if (f.minimum != kCatalog)
        minimum = f.minimum;
    if (f.maximum != kCatalog)
        maximum = f.maximum;
}

const char *showName(Show show) noexcept
{
    switch (show)
    {
    case Show::Always: return "always";
    case Show::EngineOsc: return "engine=osc";
    case Show::EngineWaveguide: return "engine=waveguide";
    case Show::EngineNoiseFx: return "engine=noisefx";
    case Show::EngineHypersaw: return "engine=hypersaw";
    case Show::EngineRecipe: return "engine=recipe";
    case Show::Osc1: return "osc>=1";
    case Show::Osc2: return "osc>=2";
    case Show::Osc3: return "osc>=3";
    case Show::Pulse1: return "pulse1";
    case Show::Pulse2: return "pulse2";
    case Show::Pulse3: return "pulse3";
    case Show::Harmony1: return "harmony1";
    case Show::Filter: return "filter";
    case Show::Ladder: return "ladder";
    case Show::Overdrive: return "overdrive";
    case Show::Envelope: return "envelope";
    case Show::RecipeFm: return "recipe=Feedback FM";
    case Show::RecipePhase: return "recipe=Phase morph";
    case Show::RecipeSpectral: return "recipe=Spectral DSF";
    case Show::RecipePrism: return "recipe=Prism";
    }
    return "always";
}

namespace
{
bool oscillatorShown(const VoiceConfig &c, int osc) noexcept
{
    return c.engine == ENGINE_OSC && osc < c.oscillatorCount;
}
bool hasPulse(const VoiceConfig &c, int osc) noexcept
{
    return oscillatorShown(c, osc) &&
           (c.oscWaveforms[osc] == WAVE_SQUARE || c.oscWaveforms[osc] == WAVE_BSP_SQUARE);
}
bool recipeIs(const VoiceConfig &c, const VoiceRecipe &recipe) noexcept
{
    return c.engine == ENGINE_RECIPE && c.recipe == &recipe;
}
}  // namespace

bool shown(Show show, const VoiceConfig &c) noexcept
{
    switch (show)
    {
    case Show::Always: return true;
    case Show::EngineOsc: return c.engine == ENGINE_OSC;
    case Show::EngineWaveguide: return c.engine == ENGINE_WAVEGUIDE;
    case Show::EngineNoiseFx: return c.engine == ENGINE_NOISEFX;
    case Show::EngineHypersaw: return c.engine == ENGINE_HYPERSAW;
    case Show::EngineRecipe: return c.engine == ENGINE_RECIPE;
    case Show::Osc1: return oscillatorShown(c, 0);
    case Show::Osc2: return oscillatorShown(c, 1);
    case Show::Osc3: return oscillatorShown(c, 2);
    case Show::Pulse1: return hasPulse(c, 0);
    case Show::Pulse2: return hasPulse(c, 1);
    case Show::Pulse3: return hasPulse(c, 2);
    case Show::Harmony1: return c.engine != ENGINE_OSC || oscillatorShown(c, 0);
    case Show::Filter: return c.hasFilter;
    case Show::Ladder: return c.hasFilter && c.filterType == FILTER_LADDER;
    case Show::Overdrive: return c.hasOverdrive;
    case Show::Envelope: return c.hasEnvelope;
    case Show::RecipeFm: return recipeIs(c, VoiceRecipes::kFeedbackFm);
    case Show::RecipePhase: return recipeIs(c, VoiceRecipes::kPhaseMorph);
    case Show::RecipeSpectral: return recipeIs(c, VoiceRecipes::kSpectralDsf);
    case Show::RecipePrism: return recipeIs(c, VoiceRecipes::kPrism);
    }
    return true;
}

uint32_t tableHash() noexcept
{
    uint32_t h = 2166136261u;
    const auto mix = [&h](const void *data, size_t n) {
        const auto *p = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < n; ++i)
        {
            h ^= p[i];
            h *= 16777619u;
        }
    };
    for (const Field &f : kFields)
    {
        mix(f.key, std::strlen(f.key) + 1);
        mix(&f.offset, sizeof f.offset);
        const uint8_t tail[4] = {static_cast<uint8_t>(f.type), f.mask,
                                 static_cast<uint8_t>(f.edit), static_cast<uint8_t>(f.show)};
        mix(tail, sizeof tail);
        mix(&f.minimum, sizeof f.minimum);
        mix(&f.maximum, sizeof f.maximum);
    }
    const uint16_t size = sizeof(PatchSnapshot);
    mix(&size, sizeof size);
    return h;
}

bool finiteFloat(float value) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

}  // namespace patchfields
