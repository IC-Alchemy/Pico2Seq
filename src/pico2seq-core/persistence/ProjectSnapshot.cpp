// ProjectSnapshot: version upgrades and sanity checks (see header).
// v1 songs upgrade to "follow the patch" so they replay as originally heard.
#include "ProjectSnapshot.h"

namespace persistence
{

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
    for (auto &voice : s.sitar)
        voice = SitarPatchSnapshot{};
}

void upgradeFromV2(ProjectSnapshot &s) noexcept
{
    for (auto &voice : s.sitar)
        voice = SitarPatchSnapshot{};
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
    return true;
}

} // namespace persistence
