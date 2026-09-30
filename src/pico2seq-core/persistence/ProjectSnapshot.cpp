// ProjectSnapshot: version upgrades and sanity checks (see header).
// v1 songs upgrade to "follow the patch" so they replay as originally heard;
// v1/v2 songs get the default effect settings (reverb mix 0: no change in sound).
#include "ProjectSnapshot.h"

#include <cstring>

namespace persistence
{

namespace
{
// Bit-pattern finite test. The firmware is built with -ffast-math
// (-ffinite-math-only): arm-none-eabi-gcc 16.1 compiles std::isfinite(x) to a
// constant "true" there (measured), so it cannot reject a corrupt NaN.
bool finiteFloat(float value) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

bool inRange(float value, float low, float high) noexcept
{
    return finiteFloat(value) && value >= low && value <= high;
}
} // namespace

void applyEffectsDefaults(EffectsSnapshot &effects) noexcept
{
    std::memset(&effects, 0, sizeof effects);
    effects.reverbMix = EffectsLimits::kReverbMixDefault;
    effects.reverbDecaySeconds = EffectsLimits::kReverbDecayDefault;
    effects.reverbDampingHz = EffectsLimits::kReverbDampingDefault;
    effects.reverbLowCutHz = EffectsLimits::kReverbLowCutDefault;
    effects.reverbDiffusion = EffectsLimits::kReverbDiffusionDefault;
    effects.reverbModDepth = EffectsLimits::kReverbModDepthDefault;
    effects.reverbModRateHz = EffectsLimits::kReverbModRateDefault;
    effects.reverbWidth = EffectsLimits::kReverbWidthDefault;
}

bool validateEffects(const EffectsSnapshot &e) noexcept
{
    using namespace EffectsLimits;
    return inRange(e.reverbMix, kReverbMixMin, kReverbMixMax) &&
           inRange(e.reverbDecaySeconds, kReverbDecayMin, kReverbDecayMax) &&
           inRange(e.reverbDampingHz, kReverbDampingMin, kReverbDampingMax) &&
           inRange(e.reverbLowCutHz, kReverbLowCutMin, kReverbLowCutMax) &&
           inRange(e.reverbDiffusion, kReverbDiffusionMin, kReverbDiffusionMax) &&
           inRange(e.reverbModDepth, kReverbModDepthMin, kReverbModDepthMax) &&
           inRange(e.reverbModRateHz, kReverbModRateMin, kReverbModRateMax) &&
           inRange(e.reverbWidth, kReverbWidthMin, kReverbWidthMax);
}

void upgradeFromV1(ProjectSnapshot &s) noexcept
{
    for (auto &voice : s.envelopes)
    {
        for (auto &track : voice.tracks)
        {
            // New lanes start silent-by-default: follow the patch on 16 steps.
            for (float &value : track.values)
                value = SequencerConstants::LANE_FOLLOWS_PATCH;
            track.stepCount = SequencerConstants::DEFAULT_STEPS_COUNT;
            track.reserved[0] = track.reserved[1] = track.reserved[2] = 0;
        }
    }
    // Offset-era file: Session converts these once the voices exist.
    s.laneModel = LANE_MODEL_OFFSETS;
    s.reserved = 0;
    upgradeFromV2(s);
}

void upgradeFromV2(ProjectSnapshot &s) noexcept
{
    applyEffectsDefaults(s.effects);
}

bool validateProjectSnapshot(const ProjectSnapshot &s) noexcept
{
    // Loop lengths of 0 or >64 mean a torn write — reject before playback.
    const auto validCount = [](uint8_t count) {
        return count != 0 && count <= SequencerConstants::MAX_STEPS_COUNT;
    };
    for (uint8_t voice = 0; voice < 4; ++voice)
    {
        for (const auto &track : s.patterns[voice].tracks)
            if (!validCount(track.stepCount))
                return false;
        for (const auto &track : s.envelopes[voice].tracks)
            if (!validCount(track.stepCount))
                return false;
        // Preset slots top at 63 here; PatchCodec enforces the tighter live count.
        if (s.settings.presetIndices[voice] > 63) // true bound checked by PatchCodec (preset count)
            return false;
    }
    const auto &set = s.settings;
    if (set.tempoBpm < 45.0f || set.tempoBpm > 200.0f)
        return false;
    if (set.currentScale > 12)
        return false;
    if (set.shuffleIndex > 15)
        return false;
    if (set.themeIndex < 0 || set.themeIndex > 9)
        return false;
    if (set.selectedVoice > 3)
        return false;
    if (s.laneModel != LANE_MODEL_OFFSETS && s.laneModel != LANE_MODEL_ABSOLUTE)
        return false;
    // A non-finite or out-of-range effect field rejects the whole file before any
    // of it can reach the audio thread.
    return validateEffects(s.effects);
}

size_t payloadSizeForVersion(uint16_t version) noexcept
{
    switch (version)
    {
    case SNAPSHOT_FORMAT_VERSION_V1: return sizeof(ProjectSnapshotV1);
    case SNAPSHOT_FORMAT_VERSION_V2: return sizeof(ProjectSnapshotV2);
    case SNAPSHOT_FORMAT_VERSION: return sizeof(ProjectSnapshot);
    default: return 0;
    }
}

bool decodeSnapshotFrame(const uint8_t header[12], const uint8_t *payload, size_t payloadCapacity,
                         ProjectSnapshot &out) noexcept
{
    const uint16_t version = frameVersion(header);
    const size_t payloadSize = payloadSizeForVersion(version);
    if (payloadSize == 0)
        return false;
    if (readFrameHeader(header, payload, payloadCapacity, static_cast<uint16_t>(payloadSize),
                        version) != FrameStatus::Ok)
        return false;
    if (payload != reinterpret_cast<const uint8_t *>(&out))
        std::memcpy(&out, payload, payloadSize);
    if (version == SNAPSHOT_FORMAT_VERSION_V1)
        upgradeFromV1(out);
    else if (version == SNAPSHOT_FORMAT_VERSION_V2)
        upgradeFromV2(out);
    return validateProjectSnapshot(out);
}

} // namespace persistence
