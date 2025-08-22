#pragma once

#include "Voice.h"
#include "../sequencer/Sequencer.h"
#include <vector>
#include <memory>
#include <functional>

/**
 * VoiceManager - Manages multiple voices for polyphonic/multitimbral synthesis
 * 
 * This class provides:
 * - Dynamic voice allocation and deallocation
 * - Voice preset management
 * - Unified audio processing for all voices
 * - Voice parameter updates and MIDI routing
 * - Memory-efficient voice management for embedded systems
 */
class VoiceManager {
public:
    // Voice allocation callback - called when voice count changes
    using VoiceCountCallback = std::function<void(uint8_t voiceCount)>;
    
    // Voice parameter update callback - called when voice parameters change
    using VoiceUpdateCallback = std::function<void(uint8_t voiceId, const VoiceState& state)>;
    
    VoiceManager(uint8_t maxVoices = 8);
    ~VoiceManager() = default;
    
    // Voice Management
    uint8_t addVoice(const VoiceConfig& config);
    uint8_t addVoice(const std::string& presetName);
    bool removeVoice(uint8_t voiceId);
    void removeAllVoices();
    
    // Voice Configuration
    bool setVoiceConfig(uint8_t voiceId, const VoiceConfig& config);
    bool setVoicePreset(uint8_t voiceId, const std::string& presetName);
    VoiceConfig* getVoiceConfig(uint8_t voiceId);
    
    // Voice State Management
    bool updateVoiceState(uint8_t voiceId, const VoiceState& state);
    VoiceState* getVoiceState(uint8_t voiceId);
    
    // Sequencer Management
    bool attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer);
    bool attachSequencer(uint8_t voiceId, Sequencer* sequencer);
    Sequencer* getSequencer(uint8_t voiceId);
    
    // Audio Processing
    void init(float sampleRate);
    float processAllVoices();
    float processVoice(uint8_t voiceId);
    
    // Voice Control
    void enableVoice(uint8_t voiceId, bool enabled = true);
    void disableVoice(uint8_t voiceId);
    bool isVoiceEnabled(uint8_t voiceId) const;
    
    // Voice Information
    uint8_t getVoiceCount() const { return static_cast<uint8_t>(voices.size()); }
    uint8_t getMaxVoices() const { return maxVoiceCount; }
    std::vector<uint8_t> getActiveVoiceIds() const;
    
    // Memory Management
    size_t getMemoryUsage() const;
    bool hasAvailableSlots() const { return voices.size() < maxVoiceCount; }
    
    // Callbacks
    void setVoiceCountCallback(VoiceCountCallback callback) { voiceCountCallback = callback; }
    void setVoiceUpdateCallback(VoiceUpdateCallback callback) { voiceUpdateCallback = callback; }
    
    // Preset Management
    static std::vector<std::string> getAvailablePresets();
    static VoiceConfig getPresetConfig(const std::string& presetName);
    
    // Global Voice Parameters
    void setGlobalVolume(float volume) { globalVolume = volume; }
    float getGlobalVolume() const { return globalVolume; }
    
    void setVoiceMix(uint8_t voiceId, float mix);
    float getVoiceMix(uint8_t voiceId) const;
    
    // Voice Routing
    void setVoiceOutput(uint8_t voiceId, uint8_t outputChannel);
    uint8_t getVoiceOutput(uint8_t voiceId) const;
    
    // Voice Parameter Control
    void setVoiceVolume(uint8_t voiceId, float volume);
    void setVoiceFrequency(uint8_t voiceId, float frequency);
    void setVoiceSlide(uint8_t voiceId, float slideTime);
    
private:
    struct ManagedVoice {
        std::unique_ptr<Voice> voice;
        uint8_t id;
        bool enabled;
        float mixLevel;
        uint8_t outputChannel;
        
        ManagedVoice(std::unique_ptr<Voice> v, uint8_t voiceId) 
            : voice(std::move(v)), id(voiceId), enabled(true), mixLevel(1.0f), outputChannel(0) {}
    };
    
    std::vector<std::unique_ptr<ManagedVoice>> voices;
    uint8_t maxVoiceCount;
    uint8_t nextVoiceId;
    float sampleRate;
    float globalVolume;
    
    // Callbacks
    VoiceCountCallback voiceCountCallback;
    VoiceUpdateCallback voiceUpdateCallback;
    
    // Helper methods
    ManagedVoice* findVoice(uint8_t voiceId);
    const ManagedVoice* findVoice(uint8_t voiceId) const;
    uint8_t generateVoiceId();
    void notifyVoiceCountChanged();
    void notifyVoiceUpdated(uint8_t voiceId, const VoiceState& state);
};

/**
 * VoiceManagerBuilder - Builder pattern for easy VoiceManager configuration
 */
class VoiceManagerBuilder {
public:
    VoiceManagerBuilder& withMaxVoices(uint8_t maxVoices) {
        maxVoiceCount = maxVoices;
        return *this;
    }
    
    VoiceManagerBuilder& withVoice(const std::string& presetName) {
        voicePresets.push_back(presetName);
        return *this;
    }
    
    VoiceManagerBuilder& withVoice(const VoiceConfig& config) {
        voiceConfigs.push_back(config);
        return *this;
    }
    
    VoiceManagerBuilder& withGlobalVolume(float volume) {
        globalVolume = volume;
        return *this;
    }
    
    VoiceManagerBuilder& withVoiceCountCallback(VoiceManager::VoiceCountCallback callback) {
        voiceCountCallback = callback;
        return *this;
    }
    
    VoiceManagerBuilder& withVoiceUpdateCallback(VoiceManager::VoiceUpdateCallback callback) {
        voiceUpdateCallback = callback;
        return *this;
    }
    
    std::unique_ptr<VoiceManager> build() {
        auto manager = std::make_unique<VoiceManager>(maxVoiceCount);
        
        manager->setGlobalVolume(globalVolume);
        
        if (voiceCountCallback) {
            manager->setVoiceCountCallback(voiceCountCallback);
        }
        
        if (voiceUpdateCallback) {
            manager->setVoiceUpdateCallback(voiceUpdateCallback);
        }
        
        // Add preset voices
        for (const auto& preset : voicePresets) {
            manager->addVoice(preset);
        }
        
        // Add custom config voices
        for (const auto& config : voiceConfigs) {
            manager->addVoice(config);
        }
        
        return manager;
    }
    
private:
    uint8_t maxVoiceCount = 8;
    float globalVolume = 1.0f;
    std::vector<std::string> voicePresets;
    std::vector<VoiceConfig> voiceConfigs;
    VoiceManager::VoiceCountCallback voiceCountCallback;
    VoiceManager::VoiceUpdateCallback voiceUpdateCallback;
};

/**
 * VoiceFactory - Factory for creating common voice configurations
 */
class VoiceFactory {
public:
    // Create a basic dual-voice setup (like current implementation)
    static std::unique_ptr<VoiceManager> createDualVoiceSetup() {
        return VoiceManagerBuilder()
            .withMaxVoices(2)
            .withVoice("analog")
            .withVoice("digital")
            .build();
    }
    
    // Create a quad-voice setup for more complex arrangements
    static std::unique_ptr<VoiceManager> createQuadVoiceSetup() {
        return VoiceManagerBuilder()
            .withMaxVoices(4)
            .withVoice("bass")
            .withVoice("lead")
            .withVoice("pad")
            .withVoice("percussion")
            .build();
    }
    
    // Create a full 8-voice polyphonic setup
    static std::unique_ptr<VoiceManager> createPolyphonicSetup() {
        auto manager = VoiceManagerBuilder()
            .withMaxVoices(8)
            .build();
            
        // Add 8 identical analog voices for polyphony
        for (int i = 0; i < 8; i++) {
            manager->addVoice("analog");
        }
        
        return manager;
    }
    
    // Create a custom setup based on user preferences
    static std::unique_ptr<VoiceManager> createCustomSetup(
        const std::vector<std::string>& presets,
        uint8_t maxVoices = 8) {
        
        auto builder = VoiceManagerBuilder().withMaxVoices(maxVoices);
        
        for (const auto& preset : presets) {
            builder.withVoice(preset);
        }
        
        return builder.build();
    }
};