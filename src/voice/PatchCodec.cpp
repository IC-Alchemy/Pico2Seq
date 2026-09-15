#include "PatchCodec.h"
#include "VoiceConfig.h"
#include "VoicePresets.h"

namespace voicecodec
{
namespace
{
constexpr uint8_t kUsePatchBases = 1u << 0;
constexpr uint8_t kBaseGate = 1u << 1;
constexpr uint8_t kBaseSlide = 1u << 2;
constexpr uint8_t kRecipeRetrigger = 1u << 3;
constexpr uint8_t kHasOverdrive = 1u << 4;
constexpr uint8_t kHasEnvelope = 1u << 5;
constexpr uint8_t kHasFilter = 1u << 6;
constexpr uint8_t kEnabled = 1u << 7;
} // namespace

void capturePatch(const VoiceConfig &c, persistence::PatchSnapshot &o) noexcept
{
    o.baseNote = c.baseNote; o.baseVelocity = c.baseVelocity; o.baseOctave = c.baseOctave;
    o.baseGateLength = c.baseGateLength; o.slideSeconds = c.slideSeconds;
    for (int i = 0; i < 3; ++i)
    {
        o.oscAmplitudes[i] = c.oscAmplitudes[i];
        o.oscDetuning[i] = c.oscDetuning[i];
        o.oscPulseWidth[i] = c.oscPulseWidth[i];
        o.harmony[i] = c.harmony[i];
        o.oscWaveforms[i] = c.oscWaveforms[i];
    }
    o.macro1 = c.macro1; o.macro2 = c.macro2; o.macro3 = c.macro3;
    o.fmModFeedback = c.fmModFeedback; o.phaseTriangleFold = c.phaseTriangleFold;
    o.spectralSubRatio = c.spectralSubRatio; o.spectralSubShape = c.spectralSubShape;
    o.prismDriftChaos = c.prismDriftChaos;
    o.noiseSourceLevel = c.noiseSourceLevel; o.noiseChaosRate = c.noiseChaosRate;
    o.filterEnvelopeAmount = c.filterEnvelopeAmount; o.filterEnvelopeFloor = c.filterEnvelopeFloor;
    o.wgT60 = c.wgT60; o.wgBrightness = c.wgBrightness; o.wgPickPosition = c.wgPickPosition;
    o.wgPickHardness = c.wgPickHardness; o.wgStiffness = c.wgStiffness; o.wgDetune = c.wgDetune;
    o.hypersawDetune = c.hypersawDetune; o.hypersawMix = c.hypersawMix;
    o.noiseDiffuseSize = c.noiseDiffuseSize; o.noiseDiffuseMix = c.noiseDiffuseMix;
    o.noiseSwarmColor = c.noiseSwarmColor; o.noiseSwarmRegen = c.noiseSwarmRegen;
    o.noiseChaosLevel = c.noiseChaosLevel;
    o.filterRes = c.filterRes; o.filterDrive = c.filterDrive;
    o.filterPassbandGain = c.filterPassbandGain; o.filterCutoffBase = c.filterCutoffBase;
    o.highPassFreq = c.highPassFreq; o.highPassRes = c.highPassRes;
    o.overdriveGain = c.overdriveGain; o.overdriveDrive = c.overdriveDrive;
    o.defaultAttack = c.defaultAttack; o.defaultDecay = c.defaultDecay;
    o.defaultSustain = c.defaultSustain; o.defaultRelease = c.defaultRelease;
    o.outputLevel = c.outputLevel;
    o.oscillatorCount = c.oscillatorCount;
    o.engine = c.engine; o.paramSet = c.paramSet;
    o.filterType = c.filterType; o.filterMode = static_cast<uint8_t>(c.filterMode);
    o.flags = 0;
    if (c.usePatchBases) o.flags |= kUsePatchBases;
    if (c.baseGate) o.flags |= kBaseGate;
    if (c.baseSlide) o.flags |= kBaseSlide;
    if (c.recipeRetrigger) o.flags |= kRecipeRetrigger;
    if (c.hasOverdrive) o.flags |= kHasOverdrive;
    if (c.hasEnvelope) o.flags |= kHasEnvelope;
    if (c.hasFilter) o.flags |= kHasFilter;
    if (c.enabled) o.flags |= kEnabled;
    // presetIndex is stamped by the caller: a VoiceConfig does not know which
    // factory preset it came from.
}

bool applyPatch(uint8_t presetIndex, const persistence::PatchSnapshot &in, VoiceConfig &out) noexcept
{
    if (presetIndex >= VoicePresets::getPresetCount())
        return false;
    out = VoicePresets::getPresetConfig(presetIndex);

    const uint8_t presetEngine = out.engine;
    const uint8_t presetParamSet = out.paramSet;
    const VoiceParameterLayout *const presetParameters = out.parameters;

    out.baseNote = in.baseNote; out.baseVelocity = in.baseVelocity; out.baseOctave = in.baseOctave;
    out.baseGateLength = in.baseGateLength; out.slideSeconds = in.slideSeconds;
    for (int i = 0; i < 3; ++i)
    {
        out.oscAmplitudes[i] = in.oscAmplitudes[i];
        out.oscDetuning[i] = in.oscDetuning[i];
        out.oscPulseWidth[i] = in.oscPulseWidth[i];
        out.harmony[i] = in.harmony[i];
        out.oscWaveforms[i] = in.oscWaveforms[i];
    }
    out.macro1 = in.macro1; out.macro2 = in.macro2; out.macro3 = in.macro3;
    out.fmModFeedback = in.fmModFeedback; out.phaseTriangleFold = in.phaseTriangleFold;
    out.spectralSubRatio = in.spectralSubRatio; out.spectralSubShape = in.spectralSubShape;
    out.prismDriftChaos = in.prismDriftChaos;
    out.noiseSourceLevel = in.noiseSourceLevel; out.noiseChaosRate = in.noiseChaosRate;
    out.filterEnvelopeAmount = in.filterEnvelopeAmount; out.filterEnvelopeFloor = in.filterEnvelopeFloor;
    out.wgT60 = in.wgT60; out.wgBrightness = in.wgBrightness; out.wgPickPosition = in.wgPickPosition;
    out.wgPickHardness = in.wgPickHardness; out.wgStiffness = in.wgStiffness; out.wgDetune = in.wgDetune;
    out.hypersawDetune = in.hypersawDetune; out.hypersawMix = in.hypersawMix;
    out.noiseDiffuseSize = in.noiseDiffuseSize; out.noiseDiffuseMix = in.noiseDiffuseMix;
    out.noiseSwarmColor = in.noiseSwarmColor; out.noiseSwarmRegen = in.noiseSwarmRegen;
    out.noiseChaosLevel = in.noiseChaosLevel;
    out.filterRes = in.filterRes; out.filterDrive = in.filterDrive;
    out.filterPassbandGain = in.filterPassbandGain; out.filterCutoffBase = in.filterCutoffBase;
    out.highPassFreq = in.highPassFreq; out.highPassRes = in.highPassRes;
    out.overdriveGain = in.overdriveGain; out.overdriveDrive = in.overdriveDrive;
    out.defaultAttack = in.defaultAttack; out.defaultDecay = in.defaultDecay;
    out.defaultSustain = in.defaultSustain; out.defaultRelease = in.defaultRelease;
    out.outputLevel = in.outputLevel;
    out.oscillatorCount = in.oscillatorCount;
    out.engine = in.engine; out.paramSet = in.paramSet;
    out.filterType = in.filterType; out.filterMode = static_cast<VoiceFilterMode>(in.filterMode);
    out.usePatchBases = in.flags & kUsePatchBases;
    out.baseGate = in.flags & kBaseGate;
    out.baseSlide = in.flags & kBaseSlide;
    out.recipeRetrigger = in.flags & kRecipeRetrigger;
    out.hasOverdrive = in.flags & kHasOverdrive;
    out.hasEnvelope = in.flags & kHasEnvelope;
    out.hasFilter = in.flags & kHasFilter;
    out.enabled = in.flags & kEnabled;

    // The preset's layout descriptor is only valid for the preset's own
    // engine/paramSet. If the saved patch re-purposed the slots, drop the
    // pointer — VoiceParameters::layout() then derives it from paramSet.
    if (out.engine != presetEngine || out.paramSet != presetParamSet)
        out.parameters = nullptr;
    else
        out.parameters = presetParameters;

    if (out.engine == ENGINE_RECIPE && out.recipe == nullptr)
    {
        out = VoicePresets::getPresetConfig(presetIndex); // safe fallback: factory preset
        return false;
    }
    return true;
}

} // namespace voicecodec
