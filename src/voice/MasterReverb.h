#pragma once

// MasterReverb.h — rpdsp::DarkReverb on the master bus, right after the delay,
// so delay repeats feed the tank and master volume / transport mute stay
// downstream of both effects.
//
// Ownership (the rule for every member below):
//   Control thread (Core 0): setters and publishSettings() write lock-free
//     targets only. They never touch the tank, its coefficients or the applied
//     state, so a fader move cannot race the audio thread.
//   Audio thread (Core 1): render() consumes the targets once per control tick
//     (every kControlQuantum frames, counted in samples so the result does not
//     depend on how the caller splits a buffer), eases the applied values toward
//     them, and is the only code that calls the engine.
//   Setup only: prepare(). It clears the whole 64 KiB tank, so it must run before
//     the audio thread is published — never from the audio callback.
//
// The tank keeps running at mix zero, so raising the mix later reveals the
// current tail. Mix is smoothed per sample here (one shared value for both
// channels); the engine itself always renders wet-only (its own mix stays at 1).

#include "../utils/AudioRam.h" // first: defines RPDSP_HOT_FUNCTION before any rpdsp header
#include "ReverbSettings.h"
#include "../utils/SpscQueue.h"
#include "../rpdsp/src/rpdsp/dark_reverb.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

// Tank storage at the fixed 16384-sample capacity (both variants compute in float):
//   Half  (default): binary16 in 32 KiB, 33,016 B object, 11-bit stored precision.
//   Float          : 24-bit stored precision, 65,784 B object.
// Half is the default because, when this was decided, the firmware's counted setup-time
// allocations with Float exceeded the linked heap by ~1.7 KB, while Half left ~30 KB before
// the allocations that were not counted (docs/audio-performance.md, "Master reverb RAM,
// stack and SRAM audit"). MasterDelay's two rings have since been merged into one block
// (64 KiB less), which by the same arithmetic leaves ~63 KB with Float and ~96 KB with Half;
// whether to change the default is a separate decision. The accounting is static:
// re-decide from the on-board [DIAG MEM] heap numbers, building with
// -DPICO2SEQ_REVERB_STORAGE_HALF=0 for Float.
#ifndef PICO2SEQ_REVERB_STORAGE_HALF
#define PICO2SEQ_REVERB_STORAGE_HALF 1
#endif

// Bench-only: -DPICO2SEQ_REVERB_BYPASS=1 never runs the tank, so the bus renders
// dry. It is the reverb-free baseline for the same-clock CPU A/B (mix zero does not
// give one: the tank keeps evolving there by design). Never a shipping build.
#ifndef PICO2SEQ_REVERB_BYPASS
#define PICO2SEQ_REVERB_BYPASS 0
#endif

class MasterReverb
{
public:
    static constexpr size_t kCapacity = 16384;
    static constexpr bool kHalfStorage = PICO2SEQ_REVERB_STORAGE_HALF != 0;
    static constexpr bool kBypass = PICO2SEQ_REVERB_BYPASS != 0;
    using Engine = rpdsp::DarkReverb<kCapacity, kHalfStorage ? rpdsp::DarkReverbStorage::Half
                                                             : rpdsp::DarkReverbStorage::Float>;
    // Names the compiled variant in the serial diagnostics, so a timing capture
    // says which build it came from.
    static constexpr const char *kVariantName =
        kBypass ? "bypass" : (kHalfStorage ? "half16384" : "float16384");

    // Control-tick period in frames (1.33 ms at 48 kHz). Also the size of the
    // scratch render() callers reserve for one span.
    static constexpr uint32_t kControlQuantum = 64;
    // Coefficient updates are eased with this time constant (~63% in 30 ms), and
    // the mix with the same one, so a fader throw or a session restore never steps.
    static constexpr float kEaseTauSeconds = 0.030f;
    // Freeze engages after decay and damping have eased toward their open extremes
    // for this many control ticks (~60 ms). The engine's own freeze sets every
    // stage gain to 1 and bypasses damping in one step; at a 5 s decay each stage
    // gain is ~0.8, so that is a 25% step in the recirculating signal, which
    // measured as a click 16-50x above the tail's high-frequency floor. Releasing a
    // freeze starts from the long decay and open damping and eases back the same way.
    static constexpr uint32_t kFreezeRampTicks = 45;

    MasterReverb();

    // --- Control thread ------------------------------------------------------
    // Independent controls: each is one lock-free store. Values are clamped into
    // ReverbParams range (non-finite -> default) before they are published.
    void setMix(float value) noexcept { publish_(pubMix_, clean_(value, ReverbParams::kMixMin, ReverbParams::kMixMax, ReverbParams::kMixDefault)); }
    void setDecaySeconds(float value) noexcept { publish_(pubDecay_, clean_(value, ReverbParams::kDecayMin, ReverbParams::kDecayMax, ReverbParams::kDecayDefault)); }
    void setDampingHz(float value) noexcept { publish_(pubDamping_, clean_(value, ReverbParams::kDampingMin, ReverbParams::kDampingMax, ReverbParams::kDampingDefault)); }
    void setLowCutHz(float value) noexcept { publish_(pubLowCut_, clean_(value, ReverbParams::kLowCutMin, ReverbParams::kLowCutMax, ReverbParams::kLowCutDefault)); }
    void setDiffusion(float value) noexcept { publish_(pubDiffusion_, clean_(value, ReverbParams::kDiffusionMin, ReverbParams::kDiffusionMax, ReverbParams::kDiffusionDefault)); }
    void setModDepth(float value) noexcept { publish_(pubModDepth_, clean_(value, ReverbParams::kModDepthMin, ReverbParams::kModDepthMax, ReverbParams::kModDepthDefault)); }
    void setModRateHz(float value) noexcept { publish_(pubModRate_, clean_(value, ReverbParams::kModRateMin, ReverbParams::kModRateMax, ReverbParams::kModRateDefault)); }
    void setWidth(float value) noexcept { publish_(pubWidth_, clean_(value, ReverbParams::kWidthMin, ReverbParams::kWidthMax, ReverbParams::kWidthDefault)); }
    void setFreeze(bool frozen) noexcept
    {
        pubFreeze_.store(frozen, std::memory_order_relaxed);
        bumpRevision_();
    }

    // The newest values published from this thread (what the UI last set).
    ReverbSettings settings() const noexcept;

    // Coherent multi-parameter change (project restore, presets): the audio thread
    // applies every field in the same control tick. The individual targets are
    // updated too, so settings() and a full queue both stay correct. Returns false
    // when the snapshot ring was full; the values still arrive through the targets.
    bool publishSettings(const ReverbSettings &settings) noexcept;

    // --- Setup only ----------------------------------------------------------
    // Clears the tank and re-derives every coefficient from the newest published
    // targets, so setters called before prepare() survive it. Not for the audio
    // callback.
    void prepare(float sampleRate);

    // --- Audio thread --------------------------------------------------------
    // `dry` is the post-delay mono bus. On return wetLeft/wetRight (each with room
    // for `frames` samples, distinct from `dry`) hold the bus's stereo output:
    // dry and wet blended with the shared smoothed mix. dry is not modified. At a
    // settled mix of zero both channels equal dry exactly, so the legacy mono bus
    // is reproduced bit-for-bit while the tank keeps running.
    void render(const float *dry, float *wetLeft, float *wetRight, uint32_t frames) noexcept;

    // --- Host-test observability (audio-owned values; read them only while no
    // other thread is rendering) ---------------------------------------------
    // applied().freeze turns true only once the engine is frozen, i.e. after the ramp.
    const ReverbSettings &appliedSettings() const noexcept { return applied_; }
    float currentMix() const noexcept { return mix_; }

private:
    struct Snapshot
    {
        ReverbSettings settings;
        uint32_t revision;
    };

    static float clean_(float value, float low, float high, float fallback) noexcept
    {
        return ReverbParams::sanitize(value, low, high, fallback);
    }
    void publish_(std::atomic<float> &target, float value) noexcept
    {
        target.store(value, std::memory_order_relaxed);
        bumpRevision_();
    }
    // Every publish ends with a release store of the new revision, so an audio
    // thread that acquires it sees all the target stores made before it.
    void bumpRevision_() noexcept
    {
        ++localRevision_;
        revision_.store(localRevision_, std::memory_order_release);
    }
    ReverbSettings readTargets_() const noexcept;

    // Audio thread: pick up newly published targets, then ease and apply them.
    void applyTargets_() noexcept;
    void blend_(const float *dry, float *wetLeft, float *wetRight, uint32_t frames) noexcept;

    // --- Control -> audio hand-off (lock-free) --------------------------------
    std::atomic<float> pubMix_{ReverbParams::kMixDefault};
    std::atomic<float> pubDecay_{ReverbParams::kDecayDefault};
    std::atomic<float> pubDamping_{ReverbParams::kDampingDefault};
    std::atomic<float> pubLowCut_{ReverbParams::kLowCutDefault};
    std::atomic<float> pubDiffusion_{ReverbParams::kDiffusionDefault};
    std::atomic<float> pubModDepth_{ReverbParams::kModDepthDefault};
    std::atomic<float> pubModRate_{ReverbParams::kModRateDefault};
    std::atomic<float> pubWidth_{ReverbParams::kWidthDefault};
    std::atomic<bool> pubFreeze_{false};
    // Bumped after every publish. The audio thread compares it with the revision
    // it last applied, which resolves "snapshot vs later single edit" ordering.
    std::atomic<uint32_t> revision_{0};
    uint32_t localRevision_ = 0; // control thread only
    SpscQueue<Snapshot, 4> snapshots_;
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<bool>::is_always_lock_free &&
                      std::atomic<uint32_t>::is_always_lock_free,
                  "Reverb controls must be lock-free");

    // --- Audio-owned state ----------------------------------------------------
    ReverbSettings targets_;   // newest values consumed from the control side
    ReverbSettings applied_;   // what the engine currently has (eased toward targets_)
    uint32_t appliedRevision_ = 0;
    uint32_t framesUntilTick_ = 0;
    bool freezeArming_ = false;       // freeze requested; damping opening before engaging
    uint32_t freezeRampTicks_ = 0;    // ticks left before the engine is told to freeze
    float mix_ = 0.0f;
    float mixGoal_ = 0.0f;
    float mixAlpha_ = 1.0f;
    float easeAlpha_ = 1.0f;
    Engine engine_;
};
