#include "VoiceManager.h"
#include <algorithm>
#include <cstring>
#include "../utils/Debug.h"
#include "../scales/scales.h" // Inject scale data into voices

/**
 * Constructor for VoiceManager
 * Initializes voice management system with specified maximum voice capacity
 *
 * @param maxVoices Maximum number of simultaneous voices this manager can handle
 * Sets up initial state: voice ID counter, sample rate (48kHz default), and global volume
 * Pre-allocates vector capacity to avoid runtime allocations on embedded systems
 */
VoiceManager::VoiceManager(uint8_t maxVoices)
    : maxVoiceCount(maxVoices), nextVoiceId(1), sampleRate(48000.0f), globalVolume(1.0f) {
    voices.reserve(maxVoiceCount);
    DBG_INFO("VoiceManager: constructed maxVoices=%u", maxVoices);
}

/**
 * Adds a new voice using a VoiceConfig structure
 * Creates and initializes a new voice with the provided configuration
 *
 * @param config VoiceConfig structure containing oscillator, filter, and envelope settings
 * @return uint8_t Unique voice ID (1-255), or 0 if no slots available
 *
 * Process: Checks capacity → generates unique ID → creates voice → initializes → adds to collection
 * Notifies any registered callbacks about voice count change
 */
uint8_t VoiceManager::addVoice(const VoiceConfig& config) {
    if (!hasAvailableSlots()) {
        DBG_WARN("VoiceManager: addVoice failed - no slots available");
        return 0; // No available slots
    }

    uint8_t voiceId = generateVoiceId();
    auto voice = std::make_unique<Voice>(voiceId, config);

    // Inject scale context to avoid global coupling inside Voice
    voice->setScaleTable(scale, SCALES_COUNT);
    voice->setCurrentScalePointer(&currentScale);

    voice->init(sampleRate);

    auto managedVoice = std::make_unique<ManagedVoice>(std::move(voice), voiceId);
    voices.push_back(std::move(managedVoice));
    DBG_INFO("VoiceManager: voice added id=%u (count=%u)", voiceId, (unsigned)getVoiceCount()+0);

    notifyVoiceCountChanged();
    return voiceId;
}

/**
 * Adds a new voice using a preset name
 * Convenience method that looks up preset configuration by name and creates voice
 *
 * @param presetName String identifier for preset ("analog", "digital", "bass", "lead", "pad", "percussion")
 * @return uint8_t Unique voice ID, or 0 if preset not found or no slots available
 *
 * Falls back to "analog" preset if specified preset name doesn't exist
 */
uint8_t VoiceManager::addVoice(const std::string& presetName) {
    VoiceConfig config = getPresetConfig(presetName);
    return addVoice(config);
}

/**
 * Removes a voice by its unique ID
 * Safely removes voice from management and cleans up resources
 *
 * @param voiceId The unique identifier of the voice to remove
 * @return bool True if voice was found and removed, false if voice ID not found
 *
 * Uses std::find_if for safe searching, then erases from vector
 * Notifies callbacks about voice count change after removal
 */
bool VoiceManager::removeVoice(uint8_t voiceId) {
    auto it = std::find_if(voices.begin(), voices.end(),
        [voiceId](const std::unique_ptr<ManagedVoice>& v) {
            return v->id == voiceId;
        });

    if (it != voices.end()) {
        voices.erase(it);
        DBG_INFO("VoiceManager: voice removed id=%u (count=%u)", voiceId, (unsigned)getVoiceCount());
        notifyVoiceCountChanged();
        return true;
    }

    DBG_WARN("VoiceManager: removeVoice failed id=%u not found", voiceId);
    return false;
}

/**
 * Removes all voices from the manager
 * Completely clears the voice collection and resets management state
 *
 * Efficiently clears all managed voices using vector::clear()
 * Notifies callbacks that voice count has changed to 0
 */
void VoiceManager::removeAllVoices() {
    voices.clear();
    DBG_INFO("VoiceManager: all voices removed");
    notifyVoiceCountChanged();
}

/**
 * Updates a voice's configuration with new settings
 * Applies new oscillator, filter, and envelope parameters to an existing voice
 *
 * @param voiceId Target voice to reconfigure
 * @param config New VoiceConfig structure with updated parameters
 * @return bool True if voice found and updated, false if voice ID not found
 *
 * Immediately applies new configuration to the voice's DSP components
 */
bool VoiceManager::setVoiceConfig(uint8_t voiceId, const VoiceConfig& config) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        managedVoice->voice->setConfig(config);
        DBG_INFO("VoiceManager: setVoiceConfig id=%u", voiceId);
        return true;
    }
    DBG_WARN("VoiceManager: setVoiceConfig failed id=%u not found", voiceId);
    return false;
}

/**
 * Applies a preset configuration to an existing voice
 * Looks up preset by name and applies its configuration to specified voice
 *
 * @param voiceId Target voice to apply preset to
 * @param presetName Name of preset to apply (see getAvailablePresets())
 * @return bool True if voice found and preset applied, false otherwise
 *
 * Convenience wrapper around setVoiceConfig() using preset system
 */
bool VoiceManager::setVoicePreset(uint8_t voiceId, const std::string& presetName) {
    VoiceConfig config = getPresetConfig(presetName);
    bool ok = setVoiceConfig(voiceId, config);
    if (ok) {
        DBG_INFO("VoiceManager: setVoicePreset id=%u preset=%s", voiceId, presetName.c_str());
    }
    return ok;
}

/**
 * Retrieves the current configuration of a voice
 * Provides read-only access to a voice's current oscillator/filter/envelope settings
 *
 * @param voiceId Voice to query
 * @return VoiceConfig* Pointer to voice's config, or nullptr if voice not found
 *
 * Useful for UI display or debugging voice parameters
 */
VoiceConfig* VoiceManager::getVoiceConfig(uint8_t voiceId) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        return &managedVoice->voice->getConfig();
    }
    DBG_WARN("VoiceManager: getVoiceConfig id=%u not found", voiceId);
    return nullptr;
}

/**
 * Updates real-time voice parameters like frequency, gate, velocity
 * Used for live performance control and MIDI input handling
 *
 * @param voiceId Voice to update
 * @param state VoiceState structure containing frequency, gate, velocity, etc.
 * @return bool True if voice found and updated, false otherwise
 *
 * Immediately applies new state to voice, triggers note on/off, pitch changes
 * Notifies registered callbacks about the voice update
 */
bool VoiceManager::updateVoiceState(uint8_t voiceId, const VoiceState& state) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        managedVoice->voice->updateParameters(state);
        // Verbose-level to avoid flooding unless explicitly enabled
        DBG_VERBOSE("VoiceManager: updateVoiceState id=%u note=%.1f vel=%.2f gate=%d filt=%.2f", voiceId, state.noteIndex, state.velocityLevel, state.isGateHigh ? 1 : 0, state.filterCutoff);
        notifyVoiceUpdated(voiceId, state);
        return true;
    }
    DBG_WARN("VoiceManager: updateVoiceState failed id=%u not found", voiceId);
    return false;
}

/**
 * Retrieves the current real-time state of a voice
 * Provides access to live parameters like current frequency, gate status, velocity
 *
 * @param voiceId Voice to query
 * @return VoiceState* Pointer to current voice state, or nullptr if voice not found
 *
 * Useful for UI feedback, visualizations, or debugging voice behavior
 */
VoiceState* VoiceManager::getVoiceState(uint8_t voiceId) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        return &managedVoice->voice->getState();
    }
    return nullptr;
}

/**
 * Attaches a sequencer to a voice using unique_ptr (transfer ownership)
 * Enables rhythmic/arpeggiated playback for the specified voice
 *
    * @param voiceId Voice to attach sequencer to
 * @param sequencer Unique pointer to sequencer instance (ownership transferred)
 * @return bool True if voice found and sequencer attached, false otherwise
 *
 * Transfers ownership of sequencer to voice - original pointer becomes invalid
 * Voice will automatically handle sequencer lifecycle
 */
bool VoiceManager::attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        managedVoice->voice->setSequencer(std::move(sequencer));
        return true;
    }
    return false;
}

/**
 * Attaches a sequencer to a voice using raw pointer (no ownership transfer)
 * Enables rhythmic/arpeggiated playback without transferring sequencer ownership
 *
 * @param voiceId Voice to attach sequencer to
 * @param sequencer Raw pointer to sequencer instance (caller retains ownership)
 * @return bool True if voice found and sequencer attached, false otherwise
 *
 * Caller is responsible for sequencer lifecycle management
 * Useful for shared sequencers or external sequencer management
 */
bool VoiceManager::attachSequencer(uint8_t voiceId, Sequencer* sequencer) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice && sequencer) {
        // For raw pointers, we don't transfer ownership
        // The Voice class needs to handle this case
        managedVoice->voice->setSequencer(sequencer);
        return true;
    }
    return false;
}

/**
 * Retrieves the sequencer attached to a specific voice
 * Provides access to voice's current sequencer for inspection or control
 *
 * @param voiceId Voice to query
 * @return Sequencer* Pointer to attached sequencer, or nullptr if none or voice not found
 *
 * Returns nullptr if no sequencer is attached to the voice
 */
Sequencer* VoiceManager::getSequencer(uint8_t voiceId) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        return managedVoice->voice->getSequencer();
    }
    DBG_WARN("VoiceManager: getSequencer id=%u not found", voiceId);
    return nullptr;
}

/**
 * Initializes or reinitializes all voices with new sample rate
 * Updates sample rate parameter and reinitializes all DSP components
 *
 * @param sr New sample rate in Hz (e.g., 44100.0f, 48000.0f)
 *
 * Must be called after changing audio system sample rate
 * Reinitializes oscillators, filters, and envelopes with new rate
 */
void VoiceManager::init(float sr) {
    sampleRate = sr;
    DBG_INFO("VoiceManager: init sampleRate=%.1f", sr);
    for (auto& managedVoice : voices) {
        if (managedVoice->voice) {
            managedVoice->voice->init(sampleRate);
        }
    }
}

/**
 * Processes all enabled voices and returns mixed output
 * Core audio processing function called every sample frame
 *
 * @return float Mixed audio output from all enabled voices (-1.0 to 1.0 range)
 *
 * Processes each enabled voice, applies individual mix levels, sums together
 * Finally applies global volume scaling before returning
 * Optimized for embedded systems with minimal branching
 */
float VoiceManager::processAllVoices() {
    float mixedOutput = 0.0f;

    for (auto& managedVoice : voices) {
        if (managedVoice->enabled && managedVoice->voice) {
            float voiceOutput = managedVoice->voice->process();
            mixedOutput += voiceOutput * managedVoice->mixLevel;
        }
    }

    return std::clamp(mixedOutput * globalVolume, -1.0f, 1.0f);
}

/**
 * Processes a single voice and returns its output
 * Individual voice processing for solo monitoring or per-voice effects
 *
 * @param voiceId Voice to process
 * @return float Audio output from specified voice (-1.0 to 1.0 range)
 *
 * Returns 0.0f if voice not found, disabled, or invalid
 * Applies individual mix level and global volume scaling
 */
float VoiceManager::processVoice(uint8_t voiceId) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->enabled && managedVoice->voice) {
        return managedVoice->voice->process() * managedVoice->mixLevel * globalVolume;
    }
    return 0.0f;
}

/**
 * Enables or disables a voice for processing
 * Controls whether voice contributes to audio output without removing it
 *
 * @param voiceId Voice to control
 * @param enabled true to enable voice processing, false to mute/disable
 *
 * Disabled voices consume memory but don't generate audio
 * Useful for muting voices temporarily without full removal
 */
void VoiceManager::enableVoice(uint8_t voiceId, bool enabled) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice) {
        managedVoice->enabled = enabled;
        DBG_INFO("VoiceManager: %s id=%u", enabled ? "enabled" : "disabled", voiceId);
    } else {
        DBG_WARN("VoiceManager: enableVoice failed id=%u", voiceId);
    }
}

/**
 * Convenience method to disable a voice
 * Immediately mutes specified voice without removing it
 *
 * @param voiceId Voice to disable
 *
 * Wrapper around enableVoice(voiceId, false) for clarity
 */
void VoiceManager::disableVoice(uint8_t voiceId) {
    enableVoice(voiceId, false);
    // enableVoice logs; nothing else here to avoid duplicate prints
}

/**
 * Checks if a voice is currently enabled for processing
 *
 * @param voiceId Voice to check
 * @return bool true if voice exists and is enabled, false otherwise
 *
 * Useful for UI feedback and voice state monitoring
 */
bool VoiceManager::isVoiceEnabled(uint8_t voiceId) const {
    const ManagedVoice* managedVoice = findVoice(voiceId);
    return managedVoice ? managedVoice->enabled : false;
}

/**
 * Returns list of all currently enabled voice IDs
 *
 * @return std::vector<uint8_t> Vector containing IDs of all enabled voices
 *
 * Optimized for embedded systems: pre-allocates exact capacity needed
 * Useful for UI voice lists, MIDI routing, or debugging
 */
std::vector<uint8_t> VoiceManager::getActiveVoiceIds() const {
    // OPTIMIZATION: Pre-allocate with exact size to avoid dynamic reallocation
    std::vector<uint8_t> activeIds;
    activeIds.reserve(voices.size()); // Reserve maximum possible size

    for (const auto& managedVoice : voices) {
        if (managedVoice->enabled) {
            activeIds.push_back(managedVoice->id);
        }
    }

    return activeIds;
}

/**
 * Calculates approximate memory usage of the voice management system
 *
 * @return size_t Total bytes allocated for VoiceManager and all managed voices
 *
 * Includes: manager object, voice vector, all ManagedVoice instances,
 * Voice objects, and their DSP components (oscillators, filters, envelopes)
 * Useful for memory profiling and embedded system resource monitoring
 */
size_t VoiceManager::getMemoryUsage() const {
    size_t totalSize = sizeof(VoiceManager);

    // Add size of voice vector and managed voices
    totalSize += voices.capacity() * sizeof(std::unique_ptr<ManagedVoice>);

    for (const auto& managedVoice : voices) {
        totalSize += sizeof(ManagedVoice);
        if (managedVoice->voice) {
            totalSize += sizeof(Voice);
            // Add approximate size of voice components
            totalSize += managedVoice->voice->getConfig().oscillatorCount * sizeof(daisysp::Oscillator);
            totalSize += sizeof(daisysp::LadderFilter); // Filter
            totalSize += sizeof(daisysp::Svf);        // High-pass filter
            totalSize += sizeof(daisysp::Adsr);       // Envelope
        }
    }

    return totalSize;
}

/**
 * Sets the individual mix level for a voice
 * Controls relative volume of voice in final mix (0.0 to 1.0)
 *
 * @param voiceId Voice to control
 * @param mix Mix level (0.0 = silent, 1.0 = full volume, clamped to range)
 *
 * Applied before global volume scaling in processAllVoices()
 * Useful for voice balancing and individual voice control
 */
void VoiceManager::setVoiceMix(uint8_t voiceId, float mix) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice) {
        managedVoice->mixLevel = std::max(0.0f, std::min(1.0f, mix));
        DBG_INFO("VoiceManager: setVoiceMix id=%u mix=%.2f", voiceId, managedVoice->mixLevel);
    } else {
        DBG_WARN("VoiceManager: setVoiceMix failed id=%u", voiceId);
    }
}

/**
 * Gets the current mix level for a voice
 *
 * @param voiceId Voice to query
 * @return float Current mix level (0.0 to 1.0), or 0.0 if voice not found
 *
 * Returns stored mix level without global volume scaling
 */
float VoiceManager::getVoiceMix(uint8_t voiceId) const {
    const ManagedVoice* managedVoice = findVoice(voiceId);
    return managedVoice ? managedVoice->mixLevel : 0.0f;
}

/**
 * Sets the output channel assignment for a voice
 * Routes voice audio to specific hardware output channel
 *
 * @param voiceId Voice to route
 * @param outputChannel Hardware output channel number (0-based indexing)
 *
 * Used for multi-output systems or stereo panning
 * Actual routing depends on underlying audio system implementation
 */
void VoiceManager::setVoiceOutput(uint8_t voiceId, uint8_t outputChannel) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice) {
        managedVoice->outputChannel = outputChannel;
        DBG_INFO("VoiceManager: setVoiceOutput id=%u ch=%u", voiceId, outputChannel);
    } else {
        DBG_WARN("VoiceManager: setVoiceOutput failed id=%u", voiceId);
    }
}

/**
 * Gets the current output channel assignment for a voice
 *
 * @param voiceId Voice to query
 * @return uint8_t Current output channel number, or 0 if voice not found
 *
 * Returns stored output channel assignment
 */
uint8_t VoiceManager::getVoiceOutput(uint8_t voiceId) const {
    const ManagedVoice* managedVoice = findVoice(voiceId);
    return managedVoice ? managedVoice->outputChannel : 0;
}

/**
 * Returns list of all available voice preset names
 *
 * @return std::vector<std::string> Vector of preset name strings
 *
 * Static method - provides consistent preset list across all VoiceManager instances
 * Presets include: analog, digital, bass, lead, pad, percussion
 * Used by UI for preset selection menus
 */
// Static methods for preset management
std::vector<std::string> VoiceManager::getAvailablePresets() {
    return {
        "analog",
        "digital",
        "bass",
        "lead",
        "square",
        "pad",
        "percussion"
    };
}

/**
 * Retrieves the VoiceConfig for a named preset
 *
 * @param presetName Name of preset to retrieve configuration for
 * @return VoiceConfig Complete voice configuration for the preset
 *
 * Static method - maps preset names to VoicePresets factory methods
 * Falls back to "analog" preset if requested preset not found
 * Used internally by addVoice() and setVoicePreset() methods
 */
VoiceConfig VoiceManager::getPresetConfig(const std::string& presetName) {
    if (presetName == "analog") {
        return VoicePresets::getAnalogVoice();
    } else if (presetName == "digital") {
        return VoicePresets::getDigitalVoice();
    } else if (presetName == "bass") {
        return VoicePresets::getBassVoice();
    } else if (presetName == "lead") {
        return VoicePresets::getLeadVoice();
    } else if (presetName == "pad") {
        return VoicePresets::getPadVoice();
    } else if (presetName == "square") {
        return VoicePresets::getSquareVoice();
    } 
    else if (presetName == "percussion") {
        return VoicePresets::getPercussionVoice();
    } else {
        // Default to analog voice if preset not found
        return VoicePresets::getAnalogVoice();
    }
}

/**
 * Private helper: finds a ManagedVoice by its unique ID
 *
 * @param voiceId ID of voice to find
 * @return ManagedVoice* Pointer to found voice, or nullptr if not found
 *
 * Uses direct iteration for better embedded performance vs std::find_if
 * Returns raw pointer to avoid ownership issues
 */
// Private helper methods - OPTIMIZED for embedded performance
VoiceManager::ManagedVoice* VoiceManager::findVoice(uint8_t voiceId) {
    // OPTIMIZATION: Use direct iteration instead of std::find_if for better embedded performance
    for (auto& voice : voices) {
        if (voice->id == voiceId) {
            return voice.get();
        }
    }
    return nullptr;
}

/**
 * Private helper: generates unique voice IDs
 *
 * @return uint8_t New unique voice ID (1-255)
 *
 * Handles ID overflow (wraps from 255 to 1)
 * Ensures uniqueness by checking against existing voices
 * Never returns 0 (reserved for error/invalid cases)
 */
uint8_t VoiceManager::generateVoiceId() {
    uint8_t id = nextVoiceId;
    nextVoiceId++;

    // Handle overflow and ensure unique IDs
    if (nextVoiceId == 0) {
        nextVoiceId = 1;
    }

    // Ensure the ID is unique (in case of overflow)
    while (findVoice(id) != nullptr) {
        id = nextVoiceId++;
        if (nextVoiceId == 0) {
            nextVoiceId = 1;
        }
    }

    return id;
}

/**
 * Private helper: notifies callbacks about voice count changes
 *
 * Calls registered voiceCountCallback with current voice count
 * Used for UI updates, MIDI routing changes, or system notifications
 * Safe to call even if no callback is registered
 */
void VoiceManager::notifyVoiceCountChanged() {
    if (voiceCountCallback) {
        voiceCountCallback(getVoiceCount());
    }
    DBG_INFO("VoiceManager: voiceCount=%u", (unsigned)getVoiceCount());
}

/**
 * Private helper: notifies callbacks about voice parameter updates
 *
 * @param voiceId ID of voice that was updated
 * @param state New voice state parameters
 *
 * Calls registered voiceUpdateCallback with voice ID and new state
 * Used for UI parameter displays, MIDI feedback, or debugging
 * Safe to call even if no callback is registered
 */
void VoiceManager::notifyVoiceUpdated(uint8_t voiceId, const VoiceState& state) {
    if (voiceUpdateCallback) {
        voiceUpdateCallback(voiceId, state);
    }
    // Verbose-only to avoid spamming the serial port during playback
    DBG_VERBOSE("VoiceManager: notifyUpdate id=%u note=%.1f vel=%.2f gate=%d", voiceId, state.noteIndex, state.velocityLevel, state.isGateHigh ? 1 : 0);
}

/**
 * Sets the volume level for a specific voice
 * Alias for setVoiceMix() - controls individual voice loudness
 *
 * @param voiceId Voice to control
 * @param volume Volume level (0.0 = silent, 1.0 = full, clamped to range)
 *
 * Convenience method for volume control terminology
 * Same as setVoiceMix() - affects individual voice level before global scaling
 */
// Voice Parameter Control Methods
void VoiceManager::setVoiceVolume(uint8_t voiceId, float volume) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        managedVoice->mixLevel = std::max(0.0f, std::min(1.0f, volume));
        DBG_INFO("VoiceManager: setVoiceVolume id=%u vol=%.2f", voiceId, managedVoice->mixLevel);
    } else {
        DBG_WARN("VoiceManager: setVoiceVolume failed id=%u", voiceId);
    }
}

/**
 * Sets the fundamental frequency for a voice
 * Controls oscillator pitch in Hz
 *
 * @param voiceId Voice to control
 * @param frequency Frequency in Hz (e.g., 440.0f for A4)
 *
 * Immediately updates voice oscillators to new frequency
 * Used for MIDI note input, pitch bend, or manual tuning
 */
void VoiceManager::setVoiceFrequency(uint8_t voiceId, float frequency) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        managedVoice->voice->setFrequency(frequency);
        DBG_VERBOSE("VoiceManager: setVoiceFrequency id=%u f=%.2f", voiceId, frequency);
    }
}

/**
 * Sets the portamento (slide) time for frequency changes
 * Controls how quickly voice glides between frequencies
 *
 * @param voiceId Voice to control
 * @param slideTime Slide time in seconds (0.0 = instant, higher = slower glide)
 *
 * Enables smooth pitch transitions between notes
 * Applied to frequency parameter changes for legato playing
 */
void VoiceManager::setVoiceSlide(uint8_t voiceId, float slideTime) {
    ManagedVoice* managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice) {
        managedVoice->voice->setSlideTime(slideTime);
    }
    DBG_VERBOSE("VoiceManager: setVoiceSlide id=%u t=%.3f", voiceId, slideTime);

}