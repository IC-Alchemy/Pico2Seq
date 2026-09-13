#include "ProjectSnapshot.h"

namespace persistence
{

bool validateProjectSnapshot(const ProjectSnapshotV1 &s) noexcept
{
    for (uint8_t voice = 0; voice < 4; ++voice)
    {
        for (uint8_t track = 0; track < PARAM_ID_COUNT; ++track)
        {
            const uint8_t count = s.patterns[voice].tracks[track].stepCount;
            if (count == 0 || count > SequencerConstants::MAX_STEPS_COUNT)
                return false;
        }
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
    return true;
}

} // namespace persistence
