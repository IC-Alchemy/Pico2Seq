// VoiceManager.cpp — VoiceManager implementation (control thread, except
// processBlock/processAllVoices which run on Core 1 and never allocate).
#include "VoiceManager.h"
#include "../utils/AudioRam.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "../utils/Debug.h"
#include "../pico2seq-core/scales/scales.h" // Inject scale data into voices
#include "Voice.h"
#include "VoicePresets.h"

namespace
{
// One-pole time constant for the master gain: fast enough to track the volume
// knob without perceptible lag, slow enough to hide steps and transport-mute
// clicks (~63% of the way in 15 ms).
constexpr float kMasterGainTauSeconds = 0.015f;
// Macro morph easing: same shape as the master gain (fast enough to track
// the fader, slow enough to hide steps), a touch slower so all six
// compressor parameters arrive together (~63% in 30 ms).
constexpr float kMacroTauSeconds = 0.030f;
// Re-applied compressor setters trip below this macro motion: during a move
// they run at most once per processBlock call, never per sample.
constexpr float kMacroApplyEpsilon = 1.0e-4f;

inline float makeSmoothingAlpha(float tauSeconds, float sampleRate) noexcept
{
    if (tauSeconds <= 0.0f || sampleRate <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (tauSeconds * sampleRate));
}
} // namespace

/**
 * @brief VoiceManager setup: voice capacity, 48 kHz default rate, master bus.
 * @param maxVoices Max simultaneous voices (firmware uses 4).
 */
VoiceManager::VoiceManager(uint8_t maxVoices)
    : maxVoiceCount(maxVoices), nextVoiceId(1), sampleRate(48000.0f), globalVolume(.8f),
      macroTarget_(kMacroDefault)
{
    voices.reserve(maxVoiceCount);
    masterGainAlpha_ = makeSmoothingAlpha(kMasterGainTauSeconds, sampleRate);
    masterGain_ = globalVolume.load(std::memory_order_relaxed);

    configureMasterCompressor_();

    masterDelay_.prepare(sampleRate);

    DBG_INFO("VoiceManager: constructed maxVoices=%u", maxVoices);
}

/**
 * Adds a voice from a VoiceConfig. Returns the new voice ID, or 0 when full.
 */
uint8_t VoiceManager::addVoice(const VoiceConfig &config)
{
    if (!hasAvailableSlots())
    {
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
    DBG_INFO("VoiceManager: voice added id=%u (count=%u)", voiceId, (unsigned)getVoiceCount() + 0);

    notifyVoiceCountChanged();
    return voiceId;
}

/**
 * Adds a voice from a preset name (unknown names fall back to Analog).
 */
uint8_t VoiceManager::addVoice(const std::string &presetName)
{
    VoiceConfig config = getPresetConfig(presetName);
    return addVoice(config);
}

/**
 * Removes a voice by ID. True when found and removed.
 */
bool VoiceManager::removeVoice(uint8_t voiceId)
{
    auto it = std::find_if(voices.begin(), voices.end(),
                           [voiceId](const std::unique_ptr<ManagedVoice> &v)
                           {
                               return v->id == voiceId;
                           });

    if (it != voices.end())
    {
        voices.erase(it);
        DBG_INFO("VoiceManager: voice removed id=%u (count=%u)", voiceId, (unsigned)getVoiceCount());
        notifyVoiceCountChanged();
        return true;
    }

    DBG_WARN("VoiceManager: removeVoice failed id=%u not found", voiceId);
    return false;
}

/**
 * Reconfigures a voice (queued for the audio thread).
 */
bool VoiceManager::setVoiceConfig(uint8_t voiceId, const VoiceConfig &config)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        managedVoice->voice->setConfig(config);
        DBG_VERBOSE("VoiceManager: setVoiceConfig id=%u", voiceId);
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
bool VoiceManager::setVoicePreset(uint8_t voiceId, const std::string &presetName)
{
    VoiceConfig config = getPresetConfig(presetName);
    bool ok = setVoiceConfig(voiceId, config);
    if (ok)
    {
        DBG_INFO("VoiceManager: setVoicePreset id=%u preset=%s", voiceId, presetName.c_str());
    }
    return ok;
}

/**
 * Control-thread read of the staged patch (for UI display, not DSP state).
 */
const VoiceConfig *VoiceManager::getVoiceConfig(uint8_t voiceId)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        return &managedVoice->voice->getRequestedConfig();
    }
    DBG_WARN("VoiceManager: getVoiceConfig id=%u not found", voiceId);
    return nullptr;
}

/**
 * Stages a sequencer step (note/gate/velocity/brightness) for the audio
 * thread; never touches live DSP state. A full queue keeps pending controls
 * for flushControlUpdates() to retry.
 */
bool VoiceManager::updateVoiceState(uint8_t voiceId, const VoiceState &state)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        managedVoice->voice->updateParameters(state);
        // No audio-owned state is read or changed on this path.
        DBG_VERBOSE("VoiceManager: updateVoiceState id=%u note=%.1f vel=%.2f gate=%d filt=%.2f", voiceId, state.noteIndex, state.velocityLevel, state.isGateHigh ? 1 : 0, state.filterCutoff);
        notifyVoiceUpdated(voiceId, state);
        return true;
    }
    DBG_WARN("VoiceManager: updateVoiceState failed id=%u not found", voiceId);
    return false;
}

/**
 * Control-thread read of the staged step (for UI feedback, not DSP state).
 */
const VoiceState *VoiceManager::getVoiceState(uint8_t voiceId)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        return &managedVoice->voice->getRequestedState();
    }
    return nullptr;
}

/**
 * Attaches a sequencer, taking ownership (setup only).
 */
bool VoiceManager::attachSequencer(uint8_t voiceId, std::unique_ptr<Sequencer> sequencer)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        managedVoice->voice->setSequencer(std::move(sequencer));
        return true;
    }
    return false;
}

/**
 * Attaches a sequencer without transferring ownership (caller keeps it).
 */
bool VoiceManager::attachSequencer(uint8_t voiceId, Sequencer *sequencer)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice && sequencer)
    {
        // Borrowed pointer: ownership stays with the caller.
        managedVoice->voice->setSequencer(sequencer);
        return true;
    }
    return false;
}

/**
 * The voice's sequencer, or nullptr when none is attached.
 */
Sequencer *VoiceManager::getSequencer(uint8_t voiceId)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        return managedVoice->voice->getSequencer();
    }
    DBG_WARN("VoiceManager: getSequencer id=%u not found", voiceId);
    return nullptr;
}

/**
 * Sets the sample rate for all voices and the master bus (control thread).
 */
void VoiceManager::init(float sr)
{
    sampleRate = sr;
    masterGainAlpha_ = makeSmoothingAlpha(kMasterGainTauSeconds, sampleRate);
    macroAlpha_ = makeSmoothingAlpha(kMacroTauSeconds, sampleRate);
    macroCurrent_ = macroTarget_.load(std::memory_order_relaxed);
    macroApplied_ = macroCurrent_;
    masterGain_ = transportMuted_.load(std::memory_order_relaxed)
                      ? 0.0f
                      : globalVolume.load(std::memory_order_relaxed);

    // Master-bus compressor shares the single macro settings curve with
    // the constructor, re-prepared for the new sample rate.
    configureMasterCompressor_();

    masterDelay_.prepare(sampleRate);
    masterDelay_.setMix(delayMix.load(std::memory_order_relaxed));
    masterDelay_.setDelaySeconds(delayTime.load(std::memory_order_relaxed));
    masterDelay_.setFeedback(delayFeedback.load(std::memory_order_relaxed));
    masterDelay_.reset();

    DBG_INFO("VoiceManager: init sampleRate=%.1f", sr);
    for (auto &managedVoice : voices)
    {
        if (managedVoice->voice)
        {
            managedVoice->voice->init(sampleRate);
        }
    }
}

void VoiceManager::configureMasterCompressor_()
{
    compressor.prepare(sampleRate);
    applyMasterCompSettings_(settingsForMacro(macroTarget_.load(std::memory_order_relaxed)));
    compressor.reset();
}

void VoiceManager::applyMasterCompSettings_(const MasterCompSettings &settings)
{
    compressor.setThresholdDb(settings.thresholdDb);
    compressor.setRatio(settings.ratio);
    compressor.setKneeWidthDb(settings.kneeDb);
    compressor.setAttackRelease(settings.attackMs, settings.releaseMs);
    compressor.setMakeupGainDb(settings.makeupDb);
}

void VoiceManager::setMasterMacro(float macro)
{
    if (macro < 0.0f)
        macro = 0.0f;
    if (macro > 1.0f)
        macro = 1.0f;
    macroTarget_.store(macro, std::memory_order_relaxed);
}

void PICO2SEQ_AUDIO_FUNC(VoiceManager::processBlock)(float *out, uint32_t n) noexcept
{
    while (n > 0)
    {
        const uint32_t count = std::min(n, kMaxBlock);
        std::fill_n(out, count, 0.0f);
        for (auto &managedVoice : voices)
        {
            if (!managedVoice->voice) continue;
            managedVoice->voice->processBlock(voiceScratch_.data(), count);
            const float mix = managedVoice->mixLevel.load(std::memory_order_relaxed);
            for (uint32_t k = 0; k < count; ++k)
                out[k] += voiceScratch_[k] * mix;
        }
        const float target = transportMuted_.load(std::memory_order_relaxed)
                                 ? 0.0f : globalVolume.load(std::memory_order_relaxed);
        const float macroTarget = macroTarget_.load(std::memory_order_relaxed);
        float gain = masterGain_;
        const float alpha = masterGainAlpha_;
        float macro = macroCurrent_;
        const float macroAlpha = macroAlpha_;
        bool macroDirty = false;
        // Delay targets are read once per block; the delay eases toward them
        // per sample, the same contract as the master gain above.
        masterDelay_.setMix(delayMix.load(std::memory_order_relaxed));
        masterDelay_.setDelaySeconds(delayTime.load(std::memory_order_relaxed));
        masterDelay_.setFeedback(delayFeedback.load(std::memory_order_relaxed));
        for (uint32_t k = 0; k < count; ++k)
        {
            const float delayed = masterDelay_.process(out[k]);
            gain += alpha * (target - gain);
            out[k] = delayed * gain;
            // Master macro morph: eased per sample, setters at most once per
            // block so expf coefficient updates never run per sample.
            macro += macroAlpha * (macroTarget - macro);
            if (!macroDirty && std::fabs(macro - macroApplied_) > kMacroApplyEpsilon)
                macroDirty = true;
            // Master-bus glue + limiter: last DSP before the DAC (AudioEngine's
            // toPcm16 clamp remains the hard ceiling for pathological sums).
            out[k] = compressor.process(out[k]);
        }
        if (macroDirty)
        {
            applyMasterCompSettings_(settingsForMacro(macro));
            macroApplied_ = macro;
        }
        macroCurrent_ = macro;
        masterGain_ = gain;
        out += count;
        n -= count;
    }
}

float PICO2SEQ_AUDIO_FUNC(VoiceManager::processAllVoices)() noexcept
{
    float sample = 0.0f;
    processBlock(&sample, 1);
    return sample;
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
void VoiceManager::enableVoice(uint8_t voiceId, bool enabled)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice)
    {
        managedVoice->enabled = enabled;
        managedVoice->voice->setEnabled(enabled);
        DBG_INFO("VoiceManager: %s id=%u", enabled ? "enabled" : "disabled", voiceId);
    }
    else
    {
        DBG_WARN("VoiceManager: enableVoice failed id=%u", voiceId);
    }
}

/** Mutes a voice without removing it. */
void VoiceManager::disableVoice(uint8_t voiceId)
{
    enableVoice(voiceId, false);
    // enableVoice logs; nothing else here to avoid duplicate prints
}

/** True when the voice exists and is enabled. */
bool VoiceManager::isVoiceEnabled(uint8_t voiceId) const
{
    const ManagedVoice *managedVoice = findVoice(voiceId);
    return managedVoice ? managedVoice->enabled : false;
}

/** All preset names, lowercased (setup/UI helper; never called by audio). */
// Static methods for preset management
std::vector<std::string> VoiceManager::getAvailablePresets()
{
    std::vector<std::string> names;
    names.reserve(VoicePresets::getPresetCount());
    for (uint8_t i = 0; i < VoicePresets::getPresetCount(); ++i)
    {
        std::string name = VoicePresets::getPresetName(i);
        for (char &c : name)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        names.push_back(std::move(name));
    }
    return names; // setup/UI helper only; never called by audio

}

/** Patch for a preset name (unknown names fall back to Analog). */
VoiceConfig VoiceManager::getPresetConfig(const std::string &presetName)
{
    return VoicePresets::getPresetConfigByName(presetName);
}

// Private helpers (control thread).
VoiceManager::ManagedVoice *VoiceManager::findVoice(uint8_t voiceId)
{
    // Direct iteration (cheaper than std::find_if on this core).
    for (auto &voice : voices)
    {
        if (voice->id == voiceId)
        {
            return voice.get();
        }
    }
    return nullptr;
}

/**
 * Next voice ID (1-255, wraps; 0 is reserved for "no voice").
 */
uint8_t VoiceManager::generateVoiceId()
{
    uint8_t id = nextVoiceId;
    nextVoiceId++;

    // Handle overflow and ensure unique IDs
    if (nextVoiceId == 0)
    {
        nextVoiceId = 1;
    }

    // Ensure the ID is unique (in case of overflow)
    while (findVoice(id) != nullptr)
    {
        id = nextVoiceId++;
        if (nextVoiceId == 0)
        {
            nextVoiceId = 1;
        }
    }

    return id;
}

/** Runs the voice-count callback, if one is registered. */
void VoiceManager::notifyVoiceCountChanged()
{
    if (voiceCountCallback)
    {
        voiceCountCallback(getVoiceCount());
    }
    DBG_INFO("VoiceManager: voiceCount=%u", (unsigned)getVoiceCount());
}

/** Runs the voice-update callback, if one is registered. */
void VoiceManager::notifyVoiceUpdated(uint8_t voiceId, const VoiceState &state)
{
    if (voiceUpdateCallback)
    {
        voiceUpdateCallback(voiceId, state);
    }
    // Verbose-only to avoid spamming the serial port during playback
    DBG_VERBOSE("VoiceManager: notifyUpdate id=%u note=%.1f vel=%.2f gate=%d", voiceId, state.noteIndex, state.velocityLevel, state.isGateHigh ? 1 : 0);
}

/**
 * Slide (portamento) time: how fast the voice glides between notes.
 */
void VoiceManager::setVoiceSlide(uint8_t voiceId, float slideTime)
{
    ManagedVoice *managedVoice = findVoice(voiceId);
    if (managedVoice && managedVoice->voice)
    {
        managedVoice->voice->setSlideTime(slideTime);
    }
    DBG_VERBOSE("VoiceManager: setVoiceSlide id=%u t=%.3f", voiceId, slideTime);
}

// Runs on the control core only; an idle pass can deliver a pending gate-off.
void VoiceManager::flushControlUpdates()
{
    for (auto &managedVoice : voices)
        managedVoice->voice->flushControlUpdates();
}

const VoiceManager::ManagedVoice *VoiceManager::findVoice(uint8_t voiceId) const
{
    for (const auto &managedVoice : voices)
        if (managedVoice->id == voiceId)
            return managedVoice.get();
    return nullptr;
}
