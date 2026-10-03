// VoiceManager.h — owns the voices (setup-time unique_ptr only; Core 1 never
// allocates) and renders the master bus:
//   voices → master delay → looper (records the mono bus, mixes the loop back in)
//   → master reverb → shared master gain → linked stereo macro compressor → L/R.
// The voices, delay, looper and gain path are mono until the reverb; the reverb is
// where the bus becomes stereo, and the compressor sees both channels through one
// detector and one gain. Control thread configures; Core 1 calls
// processStereoBlock() (or the mono wrapper processBlock()) only. Cross-core
// knobs use lock-free atomics; reverb changes may also arrive as a coherent
// snapshot (see MasterReverb.h).
#pragma once

#include "Voice.h"
#include "MasterDelay.h"
#include "MasterReverb.h"
#include "LoopEngine.h"
#include "ReverbSettings.h"
#include "DelayTiming.h"
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
    // Callbacks (control thread; not on Core 1).
    // Voice allocation callback - called when voice count changes
    using VoiceCountCallback = std::function<void(uint8_t voiceCount)>;

    // Voice parameter update callback - called when voice parameters change
    using VoiceUpdateCallback = std::function<void(uint8_t voiceId, const VoiceState &state)>;

    VoiceManager(uint8_t maxVoices = 8);
    ~VoiceManager() = default;

    // Add a voice (control thread, setup-time; never on Core 1).
    // Returns the new voice ID, or 0 when full.
    uint8_t addVoice(const VoiceConfig &config);
    uint8_t addVoice(const std::string &presetName);
    bool removeVoice(uint8_t voiceId);

    // Voice Configuration
    bool setVoiceConfig(uint8_t voiceId, const VoiceConfig &config);
    bool setVoicePreset(uint8_t voiceId, const std::string &presetName);
    const VoiceConfig *getVoiceConfig(uint8_t voiceId);

    // Voice State Management
    bool updateVoiceState(uint8_t voiceId, const VoiceState &state, uint8_t liveEnvelopeMask = 0);
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
    // Audio thread only. Renders n frames of the stereo master bus into left/right
    // (overwritten; splits larger calls into kMaxBlock chunks, n == 0 is a no-op).
    // The buffers must not overlap; passing the same pointer or a null right
    // channel renders the mono downmix into `left` instead. Both channels come out
    // of ONE compressor detector/gain, so the stereo image is never leaned on.
    // With the reverb mix at zero both channels equal the post-delay mono mix.
    void processStereoBlock(float *left, float *right, uint32_t n) noexcept;
    // Audio thread only. Mono wrapper over the same bus for legacy callers and
    // tests: out = (L + R) / 2. At reverb mix zero L == R, so this reproduces the
    // previous mono bus (voices → delay → gain → compressor) bit-for-bit.
    void processBlock(float *out, uint32_t n) noexcept;

    // Voice Control
    void enableVoice(uint8_t voiceId, bool enabled = true);
    void disableVoice(uint8_t voiceId);
    bool isVoiceEnabled(uint8_t voiceId) const;

    // Voice Information
    uint8_t getVoiceCount() const { return static_cast<uint8_t>(voices.size()); }
    uint8_t getMaxVoices() const { return maxVoiceCount; }

    // Memory Management
    bool hasAvailableSlots() const { return voices.size() < maxVoiceCount; }

    // Callbacks
    void setVoiceCountCallback(VoiceCountCallback callback) { voiceCountCallback = callback; }
    void setVoiceUpdateCallback(VoiceUpdateCallback callback) { voiceUpdateCallback = callback; }

    // Preset Management
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
    void setDelaySynced(bool synced) { delaySynced.store(synced, std::memory_order_relaxed); }
    bool getDelaySynced() const { return delaySynced.load(std::memory_order_relaxed); }
    void setDelayNoteIndex(uint8_t index) { delayNoteIndex.store(DelayTiming::clampIndex(index), std::memory_order_relaxed); }
    uint8_t getDelayNoteIndex() const { return delayNoteIndex.load(std::memory_order_relaxed); }
    void setDelayTempoBpm(float bpm) { delayTempoBpm.store(bpm, std::memory_order_relaxed); }
    float getEffectiveDelayTime() const
    {
        return getDelaySynced()
                   ? DelayTiming::secondsForIndex(getDelayNoteIndex(), delayTempoBpm.load(std::memory_order_relaxed))
                   : getDelayTime();
    }

    // Master-bus reverb (see MasterReverb.h). Control-thread targets: the audio
    // thread consumes them once per control tick and eases toward them, so no
    // setter here touches audio-owned state. Mix defaults to 0 (bypassed sound);
    // the tank keeps evolving underneath, so raising it reveals the current tail.
    void setReverbMix(float mix) noexcept { masterReverb_.setMix(mix); }
    void setReverbDecaySeconds(float seconds) noexcept { masterReverb_.setDecaySeconds(seconds); }
    void setReverbDampingHz(float hz) noexcept { masterReverb_.setDampingHz(hz); }
    void setReverbLowCutHz(float hz) noexcept { masterReverb_.setLowCutHz(hz); }
    void setReverbDiffusion(float amount) noexcept { masterReverb_.setDiffusion(amount); }
    void setReverbModDepth(float amount) noexcept { masterReverb_.setModDepth(amount); }
    void setReverbModRateHz(float hz) noexcept { masterReverb_.setModRateHz(hz); }
    void setReverbWidth(float width) noexcept { masterReverb_.setWidth(width); }
    void setReverbFreeze(bool frozen) noexcept { masterReverb_.setFreeze(frozen); }
    // Newest values published from the control thread (what the UI last set).
    ReverbSettings getReverbSettings() const noexcept { return masterReverb_.settings(); }
    // Coherent multi-parameter change (project restore): every field is applied in
    // the same audio control tick. False only if the snapshot ring was full; the
    // values still reach the audio thread through the individual targets.
    bool applyReverbSettings(const ReverbSettings &settings) noexcept { return masterReverb_.publishSettings(settings); }

    // Master-bus looper (see LoopEngine.h): records the mono bus after the delay and
    // before the reverb, and mixes the loop back in at the same point. Control-thread
    // calls go through loop(); the audio thread only runs it from renderBus_.
    LoopEngine &loop() noexcept { return loop_; }
    const LoopEngine &loop() const noexcept { return loop_; }
    // Setup only (control thread, before the audio thread runs): size the loop's packed
    // 12-bit buffer from the heap the caller measured as free, allocate it and attach it.
    // The loop never takes the last `reserveBytes` of the heap, and a buffer that cannot
    // be had leaves the looper disabled while the rest of the bus is unaffected. True
    // when a buffer is attached.
    bool allocateLoopBuffer(size_t freeHeapBytes,
                            size_t wantedBytes = LoopEngine::kDefaultBufferBytes,
                            size_t reserveBytes = LoopEngine::kHeapReserveBytes);
    size_t loopBufferBytes() const noexcept { return loopStorageBytes_; }

    void setTransportMuted(bool muted) noexcept { transportMuted_.store(muted, std::memory_order_relaxed); }

    // Voice Parameter Control
    void setVoiceSlide(uint8_t voiceId, float slideTime);

private:
    std::atomic<bool> transportMuted_{false};

    // Master-bus gain smoothing (audio thread only). globalVolume and
    // transportMuted_ are targets; processBlock() eases toward them per sample
    // so volume moves and transport mute don't step the output (zipper/click).
    float masterGain_ = 0.0f;
    // Core 1 scratch; keep off its 2 KiB stack. Each voice renders into it while the
    // mix is built; once the mix is complete the same storage holds the reverb's
    // wet left/right for one control quantum (2 * kControlQuantum floats), so the
    // stereo bus costs no extra block scratch.
    std::array<float, kMaxBlock> voiceScratch_{};
    static_assert(2 * MasterReverb::kControlQuantum <= kMaxBlock, "reverb wet scratch must fit");
    float masterGainAlpha_ = 1.0f;

    struct ManagedVoice
    {
        std::unique_ptr<Voice> voice;
        uint8_t id;
        bool enabled; // control-thread status; Voice queues the audio enable state
        std::atomic<float> mixLevel;

        ManagedVoice(std::unique_ptr<Voice> v, uint8_t voiceId)
            : voice(std::move(v)), id(voiceId), enabled(true), mixLevel(1.0f) {}
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
    std::atomic<bool> delaySynced{false};
    std::atomic<uint8_t> delayNoteIndex{DelayTiming::kDefaultNoteIndex};
    std::atomic<float> delayTempoBpm{90.0f};
    static_assert(std::atomic<float>::is_always_lock_free, "Delay controls must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free && std::atomic<uint8_t>::is_always_lock_free,
                  "Delay mode and division must be lock-free");
    MasterDelay masterDelay_;
    // The looper sits between the delay and the reverb: it records the delayed mono bus
    // and the loop it plays back reaches the reverb, gain and compressor like the voices.
    LoopEngine loop_;
    std::unique_ptr<uint8_t[]> loopStorage_; // setup-time allocation, Core 1 only reads it
    size_t loopStorageBytes_ = 0;
    // Reverb follows the delay (repeats feed the tank) and precedes master gain.
    MasterReverb masterReverb_;

    // Master-bus compressor follows delay, reverb and master gain, so dry audio,
    // repeats and the reverb tail share one linked gain reduction.
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
    // Shared body of processStereoBlock()/processBlock(): a null `right` writes
    // the mono downmix into `left`.
    void renderBus_(float *left, float *right, uint32_t n) noexcept;
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
};
