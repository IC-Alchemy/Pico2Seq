#pragma once

#include "../pico2seq-core/sequencer/SequencerDefs.h"
#include <stdint.h>
#include "VoiceManager.h"

// Forward declaration to avoid including VoiceManager.h here
class VoiceManager;

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

    // Voice states for audio synthesis
    VoiceState voiceStates[MAX_VOICES];

    // Gate states for all voices
    volatile bool gates[MAX_VOICES] = {false, false, false, false};

    // Gate timers for all voices
    GateTimer gateTimers[MAX_VOICES];

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

    /**
     * @brief Get gate state by index
     * @param voiceIndex Voice index (0-3)
     * @return Gate state or false if invalid index
     */
    volatile bool &getGate(uint8_t voiceIndex)
    {
        static volatile bool dummy = false;
        return (voiceIndex < MAX_VOICES) ? gates[voiceIndex] : dummy;
    }

    /**
     * @brief Get gate timer by index
     * @param voiceIndex Voice index (0-3)
     * @return Reference to gate timer
     */
    GateTimer &getGateTimer(uint8_t voiceIndex)
    {
        static GateTimer dummy;
        return (voiceIndex < MAX_VOICES) ? gateTimers[voiceIndex] : dummy;
    }

    void stopAllGates()
    {
        for (uint8_t i = 0; i < MAX_VOICES; i++)
        {
            gates[i] = false;
            gateTimers[i].stop();
        }
    }

    void tickAllGateTimers()
    {
        for (uint8_t i = 0; i < MAX_VOICES; i++)
        {
            gateTimers[i].tick();
            if (gateTimers[i].isExpired() && gates[i])
            {
                gates[i] = false;
            }
        }
    }
};

extern VoiceSystem voiceSystem;
