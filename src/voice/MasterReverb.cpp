// MasterReverb.cpp — see MasterReverb.h for the thread-ownership contract.
// render()/blend_()/applyTargets_() run on Core 1: no allocation, no locks, and
// nothing here clears or constructs the 64 KiB tank (prepare() is setup-only).
#include "MasterReverb.h"

#include "../utils/AudioRam.h"

#include <algorithm>
#include <cmath>

namespace
{
// A mix within this distance of its goal lands on it exactly, so a settled zero
// reproduces the dry bus bit-for-bit. It must exceed the point where the one-pole
// stalls on its own rounding: with a per-sample alpha of ~7e-4 the update falls
// below half a float ulp (6e-8 near 1.0) at ~4e-5 from the goal, so a tighter
// threshold never fires for a goal of 1. 1e-4 is -80 dB, far below audibility.
constexpr float kMixSnap = 1.0e-4f;
// The eased controls snap when this close (0.05% in the log domain, 1e-4 in the
// linear one): far below anything audible, and it makes the final value exact.
constexpr float kLogSnap = 5.0e-4f;
constexpr float kLinearSnap = 1.0e-4f;

inline float easeLog(float current, float target, float alpha) noexcept
{
    if (current == target)
        return target;
    const float logCurrent = std::log(current);
    const float distance = std::log(target) - logCurrent;
    if (distance > -kLogSnap && distance < kLogSnap)
        return target;
    return std::exp(logCurrent + distance * alpha);
}

inline float easeLinear(float current, float target, float alpha) noexcept
{
    const float distance = target - current;
    if (distance > -kLinearSnap && distance < kLinearSnap)
        return target;
    return current + distance * alpha;
}
} // namespace

MasterReverb::MasterReverb()
{
    // The tank is zeroed by its constructor; prepare() re-derives everything.
    prepare(48000.0f);
}

ReverbSettings MasterReverb::readTargets_() const noexcept
{
    ReverbSettings out;
    out.mix = pubMix_.load(std::memory_order_relaxed);
    out.decaySeconds = pubDecay_.load(std::memory_order_relaxed);
    out.dampingHz = pubDamping_.load(std::memory_order_relaxed);
    out.lowCutHz = pubLowCut_.load(std::memory_order_relaxed);
    out.diffusion = pubDiffusion_.load(std::memory_order_relaxed);
    out.modDepth = pubModDepth_.load(std::memory_order_relaxed);
    out.modRateHz = pubModRate_.load(std::memory_order_relaxed);
    out.width = pubWidth_.load(std::memory_order_relaxed);
    out.freeze = pubFreeze_.load(std::memory_order_relaxed);
    return out;
}

ReverbSettings MasterReverb::settings() const noexcept
{
    return readTargets_();
}

bool MasterReverb::publishSettings(const ReverbSettings &settings) noexcept
{
    const ReverbSettings clean = settings.sanitized();
    pubMix_.store(clean.mix, std::memory_order_relaxed);
    pubDecay_.store(clean.decaySeconds, std::memory_order_relaxed);
    pubDamping_.store(clean.dampingHz, std::memory_order_relaxed);
    pubLowCut_.store(clean.lowCutHz, std::memory_order_relaxed);
    pubDiffusion_.store(clean.diffusion, std::memory_order_relaxed);
    pubModDepth_.store(clean.modDepth, std::memory_order_relaxed);
    pubModRate_.store(clean.modRateHz, std::memory_order_relaxed);
    pubWidth_.store(clean.width, std::memory_order_relaxed);
    pubFreeze_.store(clean.freeze, std::memory_order_relaxed);
    bumpRevision_();
    return snapshots_.tryPush(Snapshot{clean, localRevision_});
}

void MasterReverb::prepare(float sampleRate)
{
    const float rate = sampleRate > 1.0f ? sampleRate : 48000.0f;
    engine_.prepare(rate); // zeroes the whole tank: setup only
    engine_.setMix(1.0f);  // wet only; the shared smoothed mix is applied in blend_()

    // Setup-time only, so draining the audio-side ring here is safe: the audio
    // thread is not rendering yet. The published targets are the newest state.
    Snapshot discard;
    while (snapshots_.tryPop(discard))
    {
    }
    targets_ = readTargets_();
    applied_ = targets_;
    appliedRevision_ = revision_.load(std::memory_order_acquire);
    engine_.setDecaySeconds(applied_.decaySeconds);
    engine_.setDampingHz(applied_.dampingHz);
    engine_.setLowCutHz(applied_.lowCutHz);
    engine_.setDiffusion(applied_.diffusion);
    engine_.setModDepth(applied_.modDepth);
    engine_.setModRateHz(applied_.modRateHz);
    engine_.setWidth(applied_.width);
    engine_.setFreeze(applied_.freeze);

    mix_ = mixGoal_ = applied_.mix;
    framesUntilTick_ = 0;
    freezeArming_ = false;
    freezeRampTicks_ = 0;
    mixAlpha_ = 1.0f - std::exp(-1.0f / (kEaseTauSeconds * rate));
    easeAlpha_ = 1.0f - std::exp(-static_cast<float>(kControlQuantum) / (kEaseTauSeconds * rate));
}

void PICO2SEQ_AUDIO_FUNC(MasterReverb::applyTargets_)() noexcept
{
    // 1. Coherent snapshots: the newest one applies as a whole.
    Snapshot snapshot;
    bool haveSnapshot = false;
    while (snapshots_.tryPop(snapshot))
        haveSnapshot = true;
    if (haveSnapshot)
    {
        targets_ = snapshot.settings;
        appliedRevision_ = snapshot.revision;
    }
    // 2. Anything published after that snapshot (a single edit, or a snapshot the
    // ring could not hold) moves the revision past it; the individual targets
    // always hold the newest value of every field.
    const uint32_t revision = revision_.load(std::memory_order_acquire);
    if (revision != appliedRevision_)
    {
        targets_ = readTargets_();
        appliedRevision_ = revision;
    }

    mixGoal_ = targets_.mix;

    // Freeze. The engine flips loop gain and damping at once (and eases its input
    // gate over 20 ms), which is not click-free on its own, so entering opens the
    // damping first and only then engages, and leaving eases decay and damping
    // back from their frozen extremes (see kFreezeRampTicks).
    if (targets_.freeze)
    {
        if (!applied_.freeze)
        {
            if (!freezeArming_)
            {
                freezeArming_ = true;
                freezeRampTicks_ = kFreezeRampTicks;
            }
            else if (freezeRampTicks_ > 0 && --freezeRampTicks_ == 0)
            {
                freezeArming_ = false;
                applied_.freeze = true;
                engine_.setFreeze(true);
            }
        }
    }
    else
    {
        freezeArming_ = false; // aborted before it engaged: damping eases back below
        if (applied_.freeze)
        {
            applied_.freeze = false;
            engine_.setFreeze(false); // gains and damping come back from the engine's stored values
            // Start from a long decay so the loop gain glides down to the requested
            // one instead of dropping in a single step; the ease below finishes it.
            applied_.decaySeconds = ReverbParams::kDecayMax;
            engine_.setDecaySeconds(applied_.decaySeconds);
        }
    }

    // Coefficient controls ease toward their targets, one bounded update per tick,
    // and only while they differ (a settled reverb does no setter work at all).
    // While a freeze is arming or held, decay and damping head for their open
    // extremes together: a frozen tank has unity loop gain and no damping, and
    // reaching that gradually keeps both steps out of the recirculating signal
    // (the damping filter also smooths a gain step, so opening it alone would
    // make the gain step's edge sharper, not softer).
    const bool frozenLike = freezeArming_ || applied_.freeze;
    const float decayGoal = frozenLike ? ReverbParams::kDecayMax : targets_.decaySeconds;
    const float dampingGoal = frozenLike ? ReverbParams::kDampingMax : targets_.dampingHz;
    float next = easeLog(applied_.decaySeconds, decayGoal, easeAlpha_);
    if (next != applied_.decaySeconds)
    {
        applied_.decaySeconds = next;
        engine_.setDecaySeconds(next);
    }
    next = easeLog(applied_.dampingHz, dampingGoal, easeAlpha_);
    if (next != applied_.dampingHz)
    {
        applied_.dampingHz = next;
        engine_.setDampingHz(next);
    }
    next = easeLog(applied_.lowCutHz, targets_.lowCutHz, easeAlpha_);
    if (next != applied_.lowCutHz)
    {
        applied_.lowCutHz = next;
        engine_.setLowCutHz(next);
    }
    next = easeLinear(applied_.diffusion, targets_.diffusion, easeAlpha_);
    if (next != applied_.diffusion)
    {
        applied_.diffusion = next;
        engine_.setDiffusion(next);
    }
    next = easeLinear(applied_.modDepth, targets_.modDepth, easeAlpha_);
    if (next != applied_.modDepth)
    {
        applied_.modDepth = next;
        engine_.setModDepth(next);
    }
    next = easeLog(applied_.modRateHz, targets_.modRateHz, easeAlpha_);
    if (next != applied_.modRateHz)
    {
        applied_.modRateHz = next;
        engine_.setModRateHz(next);
    }
    next = easeLinear(applied_.width, targets_.width, easeAlpha_);
    if (next != applied_.width)
    {
        applied_.width = next;
        engine_.setWidth(next);
    }
}

void PICO2SEQ_AUDIO_FUNC(MasterReverb::blend_)(const float *dry, float *wetLeft, float *wetRight,
                                               uint32_t frames) noexcept
{
    if (mix_ == 0.0f && mixGoal_ == 0.0f)
    {
        // Settled dry bus: both channels are the post-delay mono sample exactly.
        for (uint32_t k = 0; k < frames; ++k)
        {
            wetLeft[k] = dry[k];
            wetRight[k] = dry[k];
        }
        return;
    }
    float mix = mix_;
    const float goal = mixGoal_;
    const float alpha = mixAlpha_;
    for (uint32_t k = 0; k < frames; ++k)
    {
        if (mix != goal)
        {
            mix += alpha * (goal - mix);
            const float distance = goal - mix;
            if (distance > -kMixSnap && distance < kMixSnap)
                mix = goal;
        }
        const float d = dry[k];
        wetLeft[k] = d + (wetLeft[k] - d) * mix;
        wetRight[k] = d + (wetRight[k] - d) * mix;
    }
    mix_ = mix;
}

void PICO2SEQ_AUDIO_FUNC(MasterReverb::render)(const float *dry, float *wetLeft, float *wetRight,
                                               uint32_t frames) noexcept
{
    uint32_t done = 0;
    while (done < frames)
    {
        // Ticks fall every kControlQuantum frames of the stream, wherever the
        // caller happens to cut its buffers.
        if (framesUntilTick_ == 0)
        {
            applyTargets_();
            framesUntilTick_ = kControlQuantum;
        }
        const uint32_t span = std::min(frames - done, framesUntilTick_);
        // One mono input on both channels: the engine's L+R normalisation then
        // sees the intended level, and the tank keeps evolving at any mix.
        engine_.process(dry + done, dry + done, wetLeft + done, wetRight + done, span);
        blend_(dry + done, wetLeft + done, wetRight + done, span);
        framesUntilTick_ -= span;
        done += span;
    }
}
