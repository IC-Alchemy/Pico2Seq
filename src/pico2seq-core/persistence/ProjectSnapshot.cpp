// ProjectSnapshot: version upgrades and sanity checks (see header).
// v1 songs upgrade to "follow the patch" so they replay as originally heard;
// v1/v2 songs get the default effect settings (reverb mix 0: no change in sound);
// v1-v3 songs get the default tuning (12-EDO, tonic C, A4 440: no change in sound).
#include "ProjectSnapshot.h"

#include "../scales/scales.h"

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

void applyTuningDefaults(TuningSnapshot &tuningRecord) noexcept
{
    std::memset(&tuningRecord, 0, sizeof tuningRecord);
    tuning::Selection selection;
    tuning::Bank bank;
    tuning::defaultBank(bank);
    captureTuning(selection, bank, tuningRecord);
}

bool validateTuning(const TuningSnapshot &t) noexcept
{
    if (!tuning::find(t.tuningId) || !tuning::find(t.previousId))
        return false;
    if (t.tonic > 11)
        return false;
    const uint16_t low = static_cast<uint16_t>(tuning::kMinA4Hz * 10.0f);
    const uint16_t high = static_cast<uint16_t>(tuning::kMaxA4Hz * 10.0f);
    if (t.a4Tenths < low || t.a4Tenths > high)
        return false;
    for (const uint8_t favorite : t.favorites)
        if (favorite != tuning::kEmptySlot && !tuning::find(favorite))
            return false;
    return true;
}

void captureTuning(const tuning::Selection &selection, const tuning::Bank &bank,
                   TuningSnapshot &out) noexcept
{
    std::memset(&out, 0, sizeof out);
    out.tuningId = selection.tuningId;
    out.tonic = selection.tonic;
    out.a4Tenths = static_cast<uint16_t>(selection.a4Hz * 10.0f + 0.5f);
    out.previousId = bank.previousId;
    for (uint8_t i = 0; i < tuning::kFavoriteSlots; ++i)
        out.favorites[i] = bank.favorites[i];
}

bool restoreTuning(const TuningSnapshot &in, tuning::Selection &selection,
                   tuning::Bank &bank) noexcept
{
    if (!validateTuning(in))
        return false;
    selection.tuningId = in.tuningId;
    selection.tonic = in.tonic;
    selection.a4Hz = static_cast<float>(in.a4Tenths) / 10.0f;
    bank.previousId = in.previousId;
    for (uint8_t i = 0; i < tuning::kFavoriteSlots; ++i)
        bank.favorites[i] = in.favorites[i];
    // The scale remembered per tuning belongs to the song that was playing, not this one.
    for (uint8_t i = 0; i < tuning::kMaxLibrary; ++i)
        bank.lastScale[i] = tuning::kNoScale;
    return true;
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
    upgradeFromV3(s);
}

void upgradeFromV3(ProjectSnapshot &s) noexcept
{
    applyTuningDefaults(s.tuning);
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
    if (set.currentScale >= SCALES_COUNT)
        return false;
    if (set.shuffleIndex > 15)
        return false;
    if (set.themeIndex < 0 || set.themeIndex > 9)
        return false;
    if (set.selectedVoice > 3)
        return false;
    if (s.laneModel != LANE_MODEL_OFFSETS && s.laneModel != LANE_MODEL_ABSOLUTE)
        return false;
    // A non-finite or out-of-range effect field, or an unknown tuning, rejects the
    // whole file before any of it can reach the audio thread.
    return validateEffects(s.effects) && validateTuning(s.tuning);
}

size_t payloadSizeForVersion(uint16_t version) noexcept
{
    switch (version)
    {
    case SNAPSHOT_FORMAT_VERSION_V1: return sizeof(ProjectSnapshotV1);
    case SNAPSHOT_FORMAT_VERSION_V2: return sizeof(ProjectSnapshotV2);
    case SNAPSHOT_FORMAT_VERSION_V3: return sizeof(ProjectSnapshotV3);
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
    else if (version == SNAPSHOT_FORMAT_VERSION_V3)
        upgradeFromV3(out);
    return validateProjectSnapshot(out);
}

} // namespace persistence
