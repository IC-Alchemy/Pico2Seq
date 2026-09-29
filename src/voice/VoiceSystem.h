// VoiceSystem.h — the 4 voices' shared control-side state (0-based 0-3).
// Control-core snapshots: the sequencer owns step timing, each VoiceState owns
// gate truth. Accessors clamp out-of-range indices; audio state crosses to
// Core 1 only through Voice's control queue, never these arrays directly.
#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <stdint.h>


/**
 * @brief Control-core snapshots for the 4 voices (replaces the old per-voice
 * globals). Sequencer owns duration; VoiceState owns gate truth.
 */
struct VoiceSystem
{
    static constexpr uint8_t MAX_VOICES = 4;

    // Voice IDs from VoiceManager (opaque handles; 0 = invalid index).
    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};

    // Core-0 control snapshots. Sequencer owns duration; VoiceState owns gate truth.
    VoiceState voiceStates[MAX_VOICES];

    /**
     * @brief Voice ID by 0-based index (0-3); 0 when out of range.
     */
    uint8_t getVoiceId(uint8_t voiceIndex) const
    {
        return (voiceIndex < MAX_VOICES) ? voiceIds[voiceIndex] : 0;
    }

    /**
     * @brief Set voice ID by 0-based index (0-3); out of range is ignored.
     */
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId)
    {
        if (voiceIndex < MAX_VOICES)
        {
            voiceIds[voiceIndex] = voiceId;
        }
    }

    /**
     * @brief Voice state by 0-based index (0-3); clamps to voice 0.
     */
    VoiceState &getVoiceState(uint8_t voiceIndex)
    {
        return voiceStates[voiceIndex < MAX_VOICES ? voiceIndex : 0];
    }

    /**
     * @brief Voice state by 0-based index (0-3), const version.
     */
    const VoiceState &getVoiceState(uint8_t voiceIndex) const
    {
        return voiceStates[voiceIndex < MAX_VOICES ? voiceIndex : 0];
    }

};

extern VoiceSystem voiceSystem;
