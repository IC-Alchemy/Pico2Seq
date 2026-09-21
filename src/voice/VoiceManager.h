// VoiceManager.h — owns the voices (setup-time unique_ptr only; Core 1 never
// allocates) and renders the summed mono bus: voices → master delay → master
// gain → macro compressor. Control thread configures; Core 1 calls
// processBlock() only. Cross-core knobs use lock-free atomics.
#pragma once

#include "Voice.h"
#include "MasterDelay.h"
#include "../pico2seq-core/sequencer/Sequencer.h"
#include "../rpdsp/src/rpdsp/dynamics.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

/**
 * VoiceManager — owns up to maxVoices synth voices and renders the master bus.
 * Setup-time unique_ptr ownership; Core 1 allocates nothing and never blocks.
 */
class VoiceManager
{
public:
    // Host-test/builder helpers (control thread; not on Core 1).
    // Voice allocation callback - called when voice count changes
    using VoiceCountCallback = std::function<void(uint8_t voiceCount)>;

    // Voice parameter update callback - called when voice parameters change
    using VoiceUpdateCallback = std::function<void(uint8_t voiceId, const VoiceState &state)>;

    VoiceManager(uint8_t maxVoices = 8);
    ~VoiceManager() = default;

    // Add/remove voices (control thread, setup-time; never on Core 1).
    // Returns the new voice ID, or 0 when full.
    uint8_t addVoice(const VoiceConfig &config);
    uint8_t addVoice(const std::string &presetName);
    bool removeVoice(uint8_t voiceId);
    void removeAllVoices();

    // Voice Configuration
    bool setVoiceConfig(uint8_t voiceId, const VoiceConfig &config);
    bool setVoicePreset(uint8_t voiceId, const std::string &presetName);
    const VoiceConfig *getVoiceConfig(uint8_t voiceId);

    // Voice State Management
    bool updateVoiceState(uint8_t voiceId, const VoiceState &state);
    const VoiceState *getVoiceState(uint8_t voiceId);
    void flushControlUpdates(); // control thread, every loop including idle passes

    // Sequencer Management
    bool attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer);
    bool attachSequencer(uint8_t voiceId, Sequencer *sequencer);
    Sequencer *getSequencer(uint8_t voiceId);

    // Audio thread only: sum all voices into one sample (-1..1).
    void init(float sampleRate);
    float processAllVoices() noexcept;

    static constexpr uint32_t kMaxBlock = 256;
    // Master-bus macro compressor (last DSP before the DAC). One 0..1 knob
    // morphs all six compressor parameters along a Warm -> Glue -> Punch
    // curve; the audio thread eases toward the target so fader moves never
    // step the output. Single source of truth for init(), the audio thread,
    // and host tests.
    struct MasterCompSettings
    {
        float thresholdDb;
        float ratio;
        float kneeDb;
        float attackMs;
        float releaseMs;
        float makeupDb;
    };
    // Curve anchors: m = 0 Warm/Glue/Leveler, m = 0.5 Neutral/Mild Glue
    // (the specified mastering squeeze), m = 1 Punch/Smash/Pump.
    static constexpr MasterCompSettings kMacroWarm = {-14.0f, 2.5f, 9.0f, 30.0f, 250.0f, 2.0f};
    static constexpr MasterCompSettings kMacroCenter = {-10.0f, 1.8f, 6.0f, 15.0f, 150.0f, 1.5f};
    static constexpr MasterCompSettings kMacroPunch = {-14.0f, 6.0f, 2.0f, 6.0f, 70.0f, 4.0f};
    static constexpr float kMacroDefault = 0.5f;
    static constexpr MasterCompSettings lerpMacroSettings(MasterCompSettings a,
                                                           MasterCompSettings b,
                                                           float t) noexcept
    {
        return {a.thresholdDb + (b.thresholdDb - a.thresholdDb) * t,
                a.ratio + (b.ratio - a.ratio) * t,
                a.kneeDb + (b.kneeDb - a.kneeDb) * t,
                a.attackMs + (b.attackMs - a.attackMs) * t,
                a.releaseMs + (b.releaseMs - a.releaseMs) * t,
                a.makeupDb + (b.makeupDb - a.makeupDb) * t};
    }
    // Piecewise-linear morph through the anchors; out-of-range clamps.
    static constexpr MasterCompSettings settingsForMacro(float macro) noexcept
    {
        const float m = macro < 0.0f ? 0.0f : (macro > 1.0f ? 1.0f : macro);
        if (m <= 0.5f)
            return lerpMacroSettings(kMacroWarm, kMacroCenter, m * 2.0f);
        return lerpMacroSettings(kMacroCenter, kMacroPunch, (m - 0.5f) * 2.0f);
    }
    // Audio thread only. Overwrites n samples, splitting larger calls into blocks.
    void processBlock(float *out, uint32_t n) noexcept;
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
    static VoiceConfig getPresetConfig(const std::string &presetName);

    // Preset lookup by name; unknown names fall back to Analog.
    // Global Voice Parameters
    void setGlobalVolume(float volume) { globalVolume.store(volume, std::memory_order_relaxed); }
    float getGlobalVolume() const { return globalVolume.load(std::memory_order_relaxed); }

    // Master macro knob (control thread writes, audio thread follows).
    // 0 = Warm/Glue/Leveler, 0.5 = Neutral/Mild Glue, 1 = Punch/Smash/Pump.
    // Driven by Shift + master-volume fader; not part of the session snapshot.
    void setMasterMacro(float macro);
    float getMasterMacro() const { return macroTarget_.load(std::memory_order_relaxed); }

    // Master-bus delay (see MasterDelay.h). Control-thread targets; the
    // audio thread reads them per block and eases per sample like the gain.
    void setDelayMix(float mix) { delayMix.store(mix, std::memory_order_relaxed); }
    float getDelayMix() const { return delayMix.load(std::memory_order_relaxed); }
    void setDelayTime(float seconds) { delayTime.store(seconds, std::memory_order_relaxed); }
    float getDelayTime() const { return delayTime.load(std::memory_order_relaxed); }
    void setDelayFeedback(float feedback) { delayFeedback.store(feedback, std::memory_order_relaxed); }
    float getDelayFeedback() const { return delayFeedback.load(std::memory_order_relaxed); }

    void setVoiceMix(uint8_t voiceId, float mix);
    void setTransportMuted(bool muted) noexcept { transportMuted_.store(muted, std::memory_order_relaxed); }
    float getVoiceMix(uint8_t voiceId) const;

    // Voice Routing
    void setVoiceOutput(uint8_t voiceId, uint8_t outputChannel);
    uint8_t getVoiceOutput(uint8_t voiceId) const;

    // Voice Parameter Control
    void setVoiceVolume(uint8_t voiceId, float volume);
    void setVoiceFrequency(uint8_t voiceId, float frequency);
    void setVoiceSlide(uint8_t voiceId, float slideTime);

private:
    std::atomic<bool> transportMuted_{false};

    // Master-bus gain smoothing (audio thread only). globalVolume and
    // transportMuted_ are targets; advanceMasterGain_() eases toward them so
    // volume moves and transport mute don't step the output (zipper/click).
    float masterGain_ = 0.0f;
    std::array<float, kMaxBlock> voiceScratch_{}; // Core 1 scratch; keep off its 2 KiB stack
    float masterGainAlpha_ = 1.0f;

    struct ManagedVoice
    {
        std::unique_ptr<Voice> voice;
        uint8_t id;
        bool enabled; // control-thread status; Voice queues the audio enable state
        std::atomic<float> mixLevel;
        uint8_t outputChannel;

        ManagedVoice(std::unique_ptr<Voice> v, uint8_t voiceId)
            : voice(std::move(v)), id(voiceId), enabled(true), mixLevel(1.0f), outputChannel(0) {}
    };

    std::vector<std::unique_ptr<ManagedVoice>> voices;
    uint8_t maxVoiceCount;
    uint8_t nextVoiceId;
    float sampleRate;
    std::atomic<float> globalVolume;
    static_assert(std::atomic<float>::is_always_lock_free, "Mixer gains must be lock-free");

    // Master-bus delay state. Targets cross cores through the atomics; the
    // delay line and its filters are audio-thread-only.
    std::atomic<float> delayMix{0.0f};
    std::atomic<float> delayTime{MasterDelay::kDefaultDelaySeconds};
    std::atomic<float> delayFeedback{MasterDelay::kDefaultFeedback};
    static_assert(std::atomic<float>::is_always_lock_free, "Delay controls must be lock-free");
    MasterDelay masterDelay_;

    // Master-bus compressor follows delay and master gain, so both dry audio
    // and repeats share its gain reduction. processVoice() is a solo tap
    // that bypasses both effects and never advances their state.
    rpdsp::Compressor compressor;
    // Macro morph state: macroTarget_ is the lock-free control-thread target;
    // macroCurrent_/macroApplied_ are audio-thread only. Setters (never
    // prepare/reset) run on the audio thread while the knob moves; the gain-
    // reduction smoother hides the motion.
    std::atomic<float> macroTarget_;
    static_assert(std::atomic<float>::is_always_lock_free, "Macro target must be lock-free");
    float macroCurrent_ = kMacroDefault;
    float macroApplied_ = kMacroDefault;
    float macroAlpha_ = 1.0f;
    // Control thread only (ctor/init): full (re)configuration.
    void configureMasterCompressor_();
    // Either thread: setters only, never touches live detector state.
    void applyMasterCompSettings_(const MasterCompSettings &settings);

    // Callbacks
    VoiceCountCallback voiceCountCallback;
    VoiceUpdateCallback voiceUpdateCallback;

    // Helper methods
    ManagedVoice *findVoice(uint8_t voiceId);
    const ManagedVoice *findVoice(uint8_t voiceId) const;
    uint8_t generateVoiceId();
    void notifyVoiceCountChanged();
    void notifyVoiceUpdated(uint8_t voiceId, const VoiceState &state);
    float advanceMasterGain_() noexcept;
};

/**
 * VoiceManagerBuilder — setup-time helper that assembles a VoiceManager
 * (control thread only; the built manager still renders on Core 1).
 */
class VoiceManagerBuilder
{
public:
    VoiceManagerBuilder &withMaxVoices(uint8_t maxVoices)
    {
        maxVoiceCount = maxVoices;
        return *this;
    }

    VoiceManagerBuilder &withVoice(const std::string &presetName)
    {
        voicePresets.push_back(presetName);
        return *this;
    }

    VoiceManagerBuilder &withVoice(const VoiceConfig &config)
    {
        voiceConfigs.push_back(config);
        return *this;
    }

    VoiceManagerBuilder &withGlobalVolume(float volume)
    {
        globalVolume = volume;
        return *this;
    }

    VoiceManagerBuilder &withVoiceCountCallback(VoiceManager::VoiceCountCallback callback)
    {
        voiceCountCallback = callback;
        return *this;
    }

    VoiceManagerBuilder &withVoiceUpdateCallback(VoiceManager::VoiceUpdateCallback callback)
    {
        voiceUpdateCallback = callback;
        return *this;
    }

    std::unique_ptr<VoiceManager> build()
    {
        auto manager = std::make_unique<VoiceManager>(maxVoiceCount);

        manager->setGlobalVolume(globalVolume);

        if (voiceCountCallback)
        {
            manager->setVoiceCountCallback(voiceCountCallback);
        }

        if (voiceUpdateCallback)
        {
            manager->setVoiceUpdateCallback(voiceUpdateCallback);
        }

        // Add preset voices
        for (const auto &preset : voicePresets)
        {
            manager->addVoice(preset);
        }

        // Add custom config voices
        for (const auto &config : voiceConfigs)
        {
            manager->addVoice(config);
        }

        return manager;
    }

private:
    uint8_t maxVoiceCount = 8;
    float globalVolume = .75f;
    std::vector<std::string> voicePresets;
    std::vector<VoiceConfig> voiceConfigs;
    VoiceManager::VoiceCountCallback voiceCountCallback;
    VoiceManager::VoiceUpdateCallback voiceUpdateCallback;
};

/**
 * VoiceFactory — example setups (host tests and bring-up; firmware wires its
 * own 4 voices instead).
 */
class VoiceFactory
{
public:
    // Two-voice example: contrasting analog/digital pair.
    static std::unique_ptr<VoiceManager> createDualVoiceSetup()
    {
        return VoiceManagerBuilder()
            .withMaxVoices(2)
            .withVoice("analog")
            .withVoice("digital")
            .build();
    }

    // Four-voice example: one voice per timbre family.
    static std::unique_ptr<VoiceManager> createQuadVoiceSetup()
    {
        return VoiceManagerBuilder()
            .withMaxVoices(4)
            .withVoice("bass")
            .withVoice("lead")
            .withVoice("pad")
            .withVoice("percussion")
            .build();
    }

    // Eight-voice example: unison analog stack.
    static std::unique_ptr<VoiceManager> createPolyphonicSetup()
    {
        auto manager = VoiceManagerBuilder()
                           .withMaxVoices(8)
                           .build();

        // One analog voice per slot.
        for (int i = 0; i < 8; i++)
        {
            manager->addVoice("analog");
        }

        return manager;
    }

    // Custom example: one voice per named preset.
    static std::unique_ptr<VoiceManager> createCustomSetup(
        const std::vector<std::string> &presets,
        uint8_t maxVoices = 8)
    {

        auto builder = VoiceManagerBuilder().withMaxVoices(maxVoices);

        for (const auto &preset : presets)
        {
            builder.withVoice(preset);
        }

        return builder.build();
    }
};
