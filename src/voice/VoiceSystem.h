#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <stdint.h>


/**
 * @brief Consolidated voice system management structure
 *
 * This struct consolidates all voice-related variables that were previously
 * declared individually (voice1Id, voice2Id, etc.) into arrays for easier
 * maintenance and reduced code duplication.
 */
struct VoiceSystem
{
    static constexpr uint8_t MAX_VOICES = 4;

    // Voice IDs from VoiceManager
    uint8_t voiceIds[MAX_VOICES] = {0, 0, 0, 0};

    // Core-0 control snapshots. Sequencer owns duration; VoiceState owns gate truth.
    VoiceState voiceStates[MAX_VOICES];

    /**
     * @brief Get voice ID by index
     * @param voiceIndex Voice index (0-3)
     * @return Voice ID or 0 if invalid index
     */
    uint8_t getVoiceId(uint8_t voiceIndex) const
    {
        return (voiceIndex < MAX_VOICES) ? voiceIds[voiceIndex] : 0;
    }

    /**
     * @brief Set voice ID by index
     * @param voiceIndex Voice index (0-3)
     * @param voiceId Voice ID to set
     */
    void setVoiceId(uint8_t voiceIndex, uint8_t voiceId)
    {
        if (voiceIndex < MAX_VOICES)
        {
            voiceIds[voiceIndex] = voiceId;
        }
    }

    /**
     * @brief Get voice state by index
     * @param voiceIndex Voice index (0-3)
     * @return Reference to voice state
     */
    VoiceState &getVoiceState(uint8_t voiceIndex)
    {
        return voiceStates[voiceIndex < MAX_VOICES ? voiceIndex : 0];
    }

    /**
     * @brief Get voice state by index (const version)
     * @param voiceIndex Voice index (0-3)
     * @return Const reference to voice state
     */
    const VoiceState &getVoiceState(uint8_t voiceIndex) const
    {
        return voiceStates[voiceIndex < MAX_VOICES ? voiceIndex : 0];
    }

};

extern VoiceSystem voiceSystem;
