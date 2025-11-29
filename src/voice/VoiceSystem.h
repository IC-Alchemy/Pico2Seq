#pragma once

#include "../sequencer/SequencerDefs.h"
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

    // Gate states (only used for voices 0 and 1)
    volatile bool gates[2] = {false, false};

    // Gate timers (only used for voices 0 and 1)
    GateTimer gateTimers[2];

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
     * @brief Helper: map a UI voice index (zero-based) to the internal voiceId.
     *
     * Use this helper from UI/event code when converting a selected voice index
     * into the internal voiceId used by VoiceManager. Returns -1 when the UI
     * index is out of range or when no voiceId is assigned at that index.
     *
     * @param uiIndex UI voice index (expected 0..MAX_VOICES-1)
     * @return int16_t internal voiceId (>=0) or -1 for invalid/unassigned
     */
    inline int16_t getVoiceIdFromUIIndex(int uiIndex) const
    {
        if (uiIndex < 0 || static_cast<uint8_t>(uiIndex) >= MAX_VOICES)
            return -1;
        uint8_t v = voiceIds[static_cast<uint8_t>(uiIndex)];
        return (v == 0) ? -1 : static_cast<int16_t>(v);
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
     * @brief Get gate state by index (only for voices 0-1)
     * @param voiceIndex Voice index (0-1)
     * @return Gate state or false if invalid index
     */
    volatile bool &getGate(uint8_t voiceIndex)
    {
        static volatile bool dummy = false;
        return (voiceIndex < 2) ? gates[voiceIndex] : dummy;
    }

    /**
     * @brief Get gate timer by index (only for voices 0-1)
     * @param voiceIndex Voice index (0-1)
     * @return Reference to gate timer
     */
    GateTimer &getGateTimer(uint8_t voiceIndex)
    {
        static GateTimer dummy;
        return (voiceIndex < 2) ? gateTimers[voiceIndex] : dummy;
    }




    void stopAllGates()
    {
        for (uint8_t i = 0; i < 2; i++)
        {
            gates[i] = false;
            gateTimers[i].stop();
        }
    }

    void tickAllGateTimers()
    {
        for (uint8_t i = 0; i < 2; i++)
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
