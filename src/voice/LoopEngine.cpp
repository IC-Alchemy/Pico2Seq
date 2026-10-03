// LoopEngine.cpp — see LoopEngine.h for the contract. Everything from processBlock()
// down runs on Core 1 and never allocates, locks or waits.
#include "LoopEngine.h"
#include "../utils/AudioRam.h"

#include <algorithm>
#include <cmath>

namespace
{
// 12 bits over +-2.0: one count is 1/1024. Two units of headroom keep a loud bus and an
// overdub from clipping while the programme still sits well above the 12-bit floor.
constexpr float kQuant = 1024.0f;
constexpr float kInvQuant = 1.0f / 1024.0f;
// The Q32.32 fraction's top 24 bits, as a float 0..1 (the float mantissa holds 24).
constexpr float kFracScale = 1.0f / 16777216.0f;

// The audio path is placed in SRAM (AudioRam.h) and a callee left out of line would run from flash,
// so the small helpers below are forced inline; pack()/unpack() are placed explicitly.
[[gnu::always_inline]] inline int quantize(float value) noexcept
{
    const int q = static_cast<int>(value * kQuant + (value >= 0.0f ? 0.5f : -0.5f));
    return q > 2047 ? 2047 : (q < -2048 ? -2048 : q);
}

// Unity up to |1.5|, then a smooth knee (slope 1 at the join) toward +-2: a loud bus keeps
// its level, and layering takes saturates instead of wrapping or clipping hard.
constexpr float kKnee = 1.5f;
[[gnu::always_inline]] inline float softClip(float x) noexcept
{
    const float a = x < 0.0f ? -x : x;
    if (a <= kKnee)
        return x;
    const float y = a - kKnee;
    const float c = kKnee + 0.5f * y / (0.5f + y);
    return x < 0.0f ? -c : c;
}
} // namespace

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

size_t LoopEngine::planBufferBytes(size_t freeHeapBytes, size_t reserveBytes,
                                   size_t wantedBytes) noexcept
{
    wantedBytes -= wantedBytes % kBytesPerPair;
    if (freeHeapBytes <= reserveBytes)
        return 0;
    size_t usable = freeHeapBytes - reserveBytes;
    usable -= usable % kBytesPerPair;
    const size_t bytes = wantedBytes < usable ? wantedBytes : usable;
    return (bytes / kBytesPerPair) * 2 >= kMinSamples ? bytes : 0;
}

// Pair p holds samples 2p and 2p+1 in bytes 3p..3p+2:
//   sample 2p   = b0 | (b1 & 0x0F) << 8
//   sample 2p+1 = (b1 >> 4) | b2 << 4
int16_t PICO2SEQ_AUDIO_FUNC(LoopEngine::unpack)(const uint8_t *buffer, uint32_t index) noexcept
{
    const uint8_t *p = buffer + (index >> 1) * kBytesPerPair;
    const uint32_t raw = (index & 1u) ? (static_cast<uint32_t>(p[1] >> 4) | (static_cast<uint32_t>(p[2]) << 4))
                                      : (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1] & 0x0Fu) << 8));
    // Sign-extend the 12-bit two's complement value.
    return static_cast<int16_t>(static_cast<int16_t>(raw << 4) >> 4);
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::pack)(uint8_t *buffer, uint32_t index, int value) noexcept
{
    uint8_t *p = buffer + (index >> 1) * kBytesPerPair;
    const uint32_t u = static_cast<uint32_t>(value) & 0xFFFu;
    if (index & 1u)
    {
        p[1] = static_cast<uint8_t>((p[1] & 0x0Fu) | ((u & 0x0Fu) << 4));
        p[2] = static_cast<uint8_t>(u >> 4);
    }
    else
    {
        p[0] = static_cast<uint8_t>(u & 0xFFu);
        p[1] = static_cast<uint8_t>((p[1] & 0xF0u) | (u >> 8));
    }
}

void LoopEngine::attach(uint8_t *storage, size_t bytes, float sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0f ? sampleRate : 48000.0f;
    gainAlphaSpan_ = 1.0f - std::exp(-static_cast<float>(kSpan) / (kGainTauSeconds * sampleRate_));
    const size_t samples = storage ? (bytes / kBytesPerPair) * 2 : 0;
    if (samples >= kMinSamples)
    {
        store_ = storage;
        capacity_ = static_cast<uint32_t>(std::min<size_t>(samples, 0x7FFFFFFFu));
        state_ = State::Empty;
    }
    else
    {
        store_ = nullptr;
        capacity_ = 0;
        state_ = State::Disabled;
    }
    count_ = 0;
    pos_ = incr_ = end_ = fadePos_ = 0;
    index_ = 0;
    cur_ = next_ = acc_ = 0.0f;
    accCount_ = 0;
    passGain_ = 1.0f;
    fadeLeft_ = 0;
    appliedPeriod_ = 0;
    reconOn_ = false;
    recon1_ = recon2_ = 0.0f;
    // Start at the targets so attaching mid-session never fades the bus in.
    seqGain_ = seqVolume_.load(std::memory_order_relaxed);
    loopGain_ = loopVolume_.load(std::memory_order_relaxed);
    publish_();
}

// ---------------------------------------------------------------------------
// Control thread
// ---------------------------------------------------------------------------

namespace
{
inline float clampUnit(float v) noexcept
{
    return !(v > 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}
} // namespace

void LoopEngine::setLoopVolume(float volume) noexcept
{
    loopVolume_.store(clampUnit(volume), std::memory_order_relaxed);
}

void LoopEngine::setSequencerVolume(float volume) noexcept
{
    seqVolume_.store(clampUnit(volume), std::memory_order_relaxed);
}

void LoopEngine::setRegen(float regen) noexcept
{
    regen_.store(!(regen > kMinRegen) ? kMinRegen : (regen > 1.0f ? 1.0f : regen),
                 std::memory_order_relaxed);
}

bool LoopEngine::post_(CommandType type, uint32_t value) noexcept
{
    Command command;
    command.type = type;
    command.value = value;
    return commands_.tryPush(command);
}

bool LoopEngine::postRecord(uint32_t periodFrames) noexcept { return post_(kRecord, periodFrames); }
bool LoopEngine::postCancel() noexcept { return post_(kCancel, 0); }
bool LoopEngine::postClear() noexcept { return post_(kClear, 0); }
bool LoopEngine::postSync() noexcept { return post_(kSync, 0); }
bool LoopEngine::postRestart() noexcept { return post_(kRestart, 0); }

float LoopEngine::position() const noexcept
{
    return static_cast<float>(publishedPosition_.load(std::memory_order_relaxed)) * (1.0f / 65535.0f);
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void PICO2SEQ_AUDIO_FUNC(LoopEngine::publish_)() noexcept
{
    publishedState_.store(static_cast<uint8_t>(state_), std::memory_order_relaxed);
    float fraction = 0.0f;
    if (count_ != 0 && state_ != State::Empty && state_ != State::Disabled)
        fraction = (static_cast<float>(index_) +
                    static_cast<float>(static_cast<uint32_t>(pos_) >> 8) * kFracScale) /
                   static_cast<float>(count_);
    publishedPosition_.store(static_cast<uint32_t>(clampUnit(fraction) * 65535.0f),
                             std::memory_order_relaxed);
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::loadCache_)() noexcept
{
    if (!store_ || count_ == 0)
    {
        cur_ = next_ = 0.0f;
        return;
    }
    cur_ = static_cast<float>(unpack(store_, index_)) * kInvQuant;
    next_ = static_cast<float>(unpack(store_, index_ + 1 < count_ ? index_ + 1 : 0)) * kInvQuant;
}

// The loop's period in frames sets its speed: count_ stored samples over `period` frames.
// Called at a take's start and whenever the tempo (or the loop size) moves the period.
void PICO2SEQ_AUDIO_FUNC(LoopEngine::setPeriod_)(uint32_t periodFrames) noexcept
{
    const uint32_t period = periodFrames < 2 ? 2 : periodFrames;
    appliedPeriod_ = period;
    end_ = static_cast<uint64_t>(count_) << 32;
    // Round up so the head reaches the end on frame `period`, not one frame later.
    const uint64_t incr = (end_ + period - 1) / period;
    incr_ = incr != 0 ? incr : 1;
    const float rate = static_cast<float>(count_) / static_cast<float>(period); // stored per frame
    uint32_t edge = static_cast<uint32_t>(kEdgeFadeSeconds * sampleRate_ * rate + 0.5f);
    const uint32_t most = std::max<uint32_t>(count_ / 4, 1u);
    edge = std::min(std::max<uint32_t>(edge, 1u), most);
    edgeSamples_ = edge;
    edgeInv_ = 1.0f / static_cast<float>(edge);
    // A decimated store has images above its own Nyquist; two poles soften them.
    reconOn_ = rate < kReconMaxRate;
    reconAlpha_ = reconOn_ ? 1.0f - std::exp(-6.2831853f * 0.45f * rate) : 1.0f;
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::beginTake_)(uint32_t periodFrames) noexcept
{
    if (periodFrames == 0 || !store_ || state_ == State::Recording ||
        state_ == State::Overdubbing || state_ == State::Disabled)
        return;
    if (state_ == State::Empty)
    {
        const uint32_t samples = std::min(capacity_, periodFrames);
        if (samples < 2)
            return;
        count_ = samples;
        passGain_ = 1.0f;
        setPeriod_(periodFrames);
        pos_ = 0;
        index_ = 0;
        cur_ = next_ = 0.0f;
        acc_ = 0.0f;
        accCount_ = 0;
        fadeLeft_ = 0;
        recon1_ = recon2_ = 0.0f;
        state_ = State::Recording;
    }
    else
    {
        // Layer a new take over the playing loop, from its top.
        setPeriod_(periodFrames);
        snapToStart_();
        state_ = State::Overdubbing;
    }
    takes_.fetch_add(1, std::memory_order_relaxed);
}

// Jump the head to the start of the loop. The old head keeps running for a few frames and
// is crossfaded out, so a sync that really moves the loop does not click.
void PICO2SEQ_AUDIO_FUNC(LoopEngine::snapToStart_)() noexcept
{
    if (state_ == State::Playing || state_ == State::Overdubbing)
    {
        fadePos_ = pos_;
        fadeLeft_ = kSnapFadeFrames;
    }
    pos_ = 0;
    index_ = 0;
    acc_ = 0.0f;
    accCount_ = 0;
    loadCache_();
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::drainCommands_)() noexcept
{
    Command command;
    while (commands_.tryPop(command))
    {
        switch (command.type)
        {
        case kRecord:
            beginTake_(command.value);
            break;
        case kCancel:
            if (state_ == State::Recording)
            {
                state_ = State::Empty;
                count_ = 0;
                fadeLeft_ = 0;
            }
            break;
        case kClear:
            if (store_)
            {
                state_ = State::Empty;
                count_ = 0;
                passGain_ = 1.0f;
                acc_ = 0.0f;
                accCount_ = 0;
                fadeLeft_ = 0;
            }
            break;
        case kSync:
            if (state_ == State::Playing)
            {
                // Distance from the seam, in stored samples (Q32.32). Within a few
                // milliseconds the audio clock and the step clock agree: leave it be.
                const uint64_t distance = pos_ < (end_ >> 1) ? pos_ : end_ - pos_;
                if (distance > static_cast<uint64_t>(kSyncToleranceFrames) * incr_)
                    snapToStart_();
            }
            break;
        case kRestart:
            if (state_ == State::Playing)
                snapToStart_();
            break;
        default:
            break;
        }
    }
}

// Fold the frames spent on `index` into the stored sample. A first take stores the plain
// mean; an overdub keeps the old sample at the gain it was just heard at and adds the new.
void PICO2SEQ_AUDIO_FUNC(LoopEngine::bake_)(uint32_t index) noexcept
{
    const bool first = state_ == State::Recording;
    float value;
    if (accCount_ != 0)
    {
        const float mean = acc_ / static_cast<float>(accCount_);
        value = first ? mean : cur_ * passGain_ + mean;
    }
    else
    {
        value = first ? 0.0f : cur_ * passGain_;
    }
    acc_ = 0.0f;
    accCount_ = 0;
    pack(store_, index, quantize(softClip(value)));
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::onWrap_)() noexcept
{
    switch (state_)
    {
    case State::Recording:
    case State::Overdubbing:
        // The buffer now holds the loop as it was heard; play it at unity from here.
        state_ = State::Playing;
        passGain_ = 1.0f;
        recon1_ = recon2_ = 0.0f;
        break;
    case State::Playing:
        passGain_ *= regenNow_;
        if (passGain_ < kMinPassGain)
        {
            state_ = State::Empty; // faded out: nothing left to play
            count_ = 0;
        }
        break;
    default:
        break;
    }
}

// The head crossed into another stored sample, or off the end of the loop.
void PICO2SEQ_AUDIO_FUNC(LoopEngine::advance_)() noexcept
{
    const bool writing = state_ == State::Recording || state_ == State::Overdubbing;
    const bool wrapped = pos_ >= end_;
    const uint32_t target = wrapped ? count_ : static_cast<uint32_t>(pos_ >> 32);
    while (index_ < target)
    {
        if (writing)
            bake_(index_);
        ++index_;
        cur_ = next_;
        if (index_ < count_)
            next_ = static_cast<float>(unpack(store_, index_ + 1 < count_ ? index_ + 1 : 0)) * kInvQuant;
    }
    if (!wrapped)
        return;
    pos_ -= end_;
    if (pos_ >= end_)
        pos_ = 0; // a head that outruns the whole loop in one frame has nothing to keep
    index_ = 0;
    onWrap_();
    if (count_ == 0)
        return; // faded out
    loadCache_();
    const uint32_t again = std::min(static_cast<uint32_t>(pos_ >> 32), count_ - 1);
    while (index_ < again)
    {
        ++index_;
        cur_ = next_;
        next_ = static_cast<float>(unpack(store_, index_ + 1 < count_ ? index_ + 1 : 0)) * kInvQuant;
    }
}

// Interpolated sample at an arbitrary head position (the fading-out head after a snap).
float PICO2SEQ_AUDIO_FUNC(LoopEngine::readAt_)(uint64_t position) const noexcept
{
    const uint32_t i = static_cast<uint32_t>(position >> 32);
    const float frac = static_cast<float>(static_cast<uint32_t>(position) >> 8) * kFracScale;
    const float a = static_cast<float>(unpack(store_, i)) * kInvQuant;
    const float b = static_cast<float>(unpack(store_, i + 1 < count_ ? i + 1 : 0)) * kInvQuant;
    return a + (b - a) * frac;
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::renderSpan_)(float *mix, uint32_t n) noexcept
{
    drainCommands_();

    // Fader targets ease once per span and ramp linearly across it.
    const float seqTarget = seqVolume_.load(std::memory_order_relaxed);
    const float loopTarget = loopVolume_.load(std::memory_order_relaxed);
    const float seq0 = seqGain_;
    float seq1 = seq0 + gainAlphaSpan_ * (seqTarget - seq0);
    if (std::fabs(seqTarget - seq1) < 1.0e-5f)
        seq1 = seqTarget;
    seqGain_ = seq1;
    const float loop0 = loopGain_;
    float loop1 = loop0 + gainAlphaSpan_ * (loopTarget - loop0);
    if (std::fabs(loopTarget - loop1) < 1.0e-5f)
        loop1 = loopTarget;
    loopGain_ = loop1;

    const bool active = state_ == State::Recording || state_ == State::Playing ||
                        state_ == State::Overdubbing;
    if (!active)
    {
        // Nothing to play or record. At unity the bus passes through untouched.
        if (seq0 == 1.0f && seq1 == 1.0f)
            return;
        const float step = (seq1 - seq0) / static_cast<float>(n);
        float gain = seq0;
        for (uint32_t k = 0; k < n; ++k)
        {
            gain += step;
            mix[k] *= gain;
        }
        return;
    }

    regenNow_ = regen_.load(std::memory_order_relaxed);
    const uint32_t period = periodFrames_.load(std::memory_order_relaxed);
    if (period != 0 && period != appliedPeriod_ && state_ != State::Recording)
        setPeriod_(period);

    const float invN = 1.0f / static_cast<float>(n);
    const float seqStep = (seq1 - seq0) * invN;
    const float loopStep = (loop1 - loop0) * invN;
    float seqNow = seq0;
    float loopNow = loop0;
    for (uint32_t k = 0; k < n; ++k)
    {
        const float live = mix[k];
        seqNow += seqStep;
        loopNow += loopStep;
        float out = live * seqNow;
        if (state_ != State::Empty)
        {
            // Recording tap: the bus before the sequencer volume.
            if (state_ == State::Recording || state_ == State::Overdubbing)
            {
                acc_ += live;
                ++accCount_;
            }
            if (state_ != State::Recording)
            {
                const float frac = static_cast<float>(static_cast<uint32_t>(pos_) >> 8) * kFracScale;
                float s = cur_ + (next_ - cur_) * frac;
                // Short fade either side of the seam so the wrap never clicks.
                if (index_ < edgeSamples_ || index_ + edgeSamples_ >= count_)
                {
                    const float t = static_cast<float>(index_) + frac;
                    const float rem = static_cast<float>(count_) - t;
                    const float w = (t < rem ? t : rem) * edgeInv_;
                    if (w < 1.0f)
                        s *= w;
                }
                if (fadeLeft_ != 0)
                {
                    const float t = static_cast<float>(fadeLeft_) * (1.0f / static_cast<float>(kSnapFadeFrames));
                    s = s * (1.0f - t) + readAt_(fadePos_) * t;
                    fadePos_ += incr_;
                    if (fadePos_ >= end_)
                        fadePos_ -= end_;
                    if (fadePos_ >= end_)
                        fadePos_ = 0;
                    --fadeLeft_;
                }
                if (reconOn_)
                {
                    recon1_ += reconAlpha_ * (s - recon1_);
                    recon2_ += reconAlpha_ * (recon1_ - recon2_);
                    s = recon2_;
                }
                out += s * passGain_ * loopNow;
            }
            pos_ += incr_;
            if (pos_ >= end_ || static_cast<uint32_t>(pos_ >> 32) != index_)
                advance_();
        }
        mix[k] = out;
    }
}

void PICO2SEQ_AUDIO_FUNC(LoopEngine::processBlock)(float *mix, uint32_t n) noexcept
{
    while (n != 0)
    {
        const uint32_t span = n < kSpan ? n : kSpan;
        renderSpan_(mix, span);
        mix += span;
        n -= span;
    }
    publish_();
}
