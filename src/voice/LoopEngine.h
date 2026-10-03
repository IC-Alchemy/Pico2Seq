// LoopEngine.h — tempo-synced audio looper on the mono master bus.
//
// Musical role: tap the loop button and, at the next loop boundary, the engine records the
// mono mix (voices + delay, BEFORE the reverb) for exactly the chosen number of steps (4, 8
// or 16), then plays it back in sync, mixed into the same bus ahead of the reverb, so the
// room sits on the loop like it sits on everything else. Every further tap LAYERS: one more
// pass mixes the live bus into the stored loop and writes the sum back over it, so any
// number of layers live in the one small buffer. "Regen" says how much of the loop is left
// on each repeat (100% = it never fades).
//
// Technical role: audio-thread DSP plus the small cross-core surface around it.
//
//   * Storage is one packed 12-bit buffer (two samples in three bytes, +-2.0 full scale)
//     allocated once at setup (VoiceManager::allocateLoopBuffer). A bar does not always fit
//     at 48 kHz, so the buffer always holds the WHOLE loop and the store rate follows:
//     stored samples = min(capacity, loop frames), box-averaged on the way in and linearly
//     interpolated + smoothed on the way out. Short loops therefore run at the full rate;
//     a slow bar trades bandwidth for length (docs/audio-performance.md has the table).
//   * One head drives everything. `pos_` walks the stored samples; the same step reads the
//     old sample, plays it, and writes the new one behind itself, so recording, overdubbing
//     and playing are one loop with different write rules.
//   * Tempo sync is by construction: the loop is a fixed number of stored samples played
//     over the CURRENT period (steps x frames per step), so a tempo change varispeeds the
//     loop instead of leaving it behind the grid. Core 0 re-aligns the wrap to the step
//     clock with Sync commands; a head already within kSyncToleranceFrames of the seam is
//     left alone, so jitter never causes clicks, and a real offset snaps with a short
//     crossfade.
//   * Regen is a per-pass playback gain, not a rewrite of the buffer: the stored loop is
//     never degraded by repeating it. A layer pass is a repeat: it plays the old loop at the
//     gain that repeat would have had, mixes the live bus in and bakes the sum back
//     (old * gain + live), so the buffer then holds exactly what was heard and the next
//     plain pass starts at unity again.
//   * Layers chain. A Record that arrives while a pass is still running (the player tapped
//     during the take or during a layer, or the step clock reached the boundary a hair before
//     the audio thread did) queues one more layer that starts the instant that pass ends, so
//     a run of taps is a run of back-to-back passes with no plain repeat between them. Which
//     side of the seam the command lands on changes nothing: a layer always mixes the old
//     loop at the previous pass's gain times regen (a freshly written loop counts as unity).
//
// Threading follows the rest of the master bus: Core 0 owns the setters and post*()
// calls (one producer), Core 1 owns everything else and only calls processBlock().
// Nothing here allocates, locks or waits. Keep audio-thread state out of the setters.
#pragma once

#include "../utils/SpscQueue.h"
#include "LoopTiming.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class LoopEngine
{
public:
    enum class State : uint8_t
    {
        Disabled,    // no buffer was attached (not enough heap); the bus passes through
        Empty,       // nothing recorded (or the last loop faded out)
        Recording,   // first take: writing the loop, loop output silent
        Playing,     // loop playing, decaying by regen on every repeat
        Overdubbing, // one pass mixing the live bus into the loop and writing it back
    };

    // --- Storage ---------------------------------------------------------------
    static constexpr size_t kBytesPerPair = 3;       // two 12-bit samples
    static constexpr uint32_t kMinSamples = 2048;    // below this the loop is not worth having
    // Default budget: 64 KiB = 43,690 samples = 0.9 s at the full 48 kHz. Layers mix into the
    // same loop, so memory does not grow with the number of layers. At this size 4 steps are
    // full rate from 66 BPM, 8 steps from 132 BPM, and a bar at 120 BPM is stored at about
    // 22 kHz (docs/audio-performance.md has the table).
    static constexpr size_t kDefaultBufferBytes = 64u * 1024u;
    // Heap left alone when sizing the buffer: the audio buffer pool, LittleFS and the
    // other things that allocate after the voices exist.
    static constexpr size_t kHeapReserveBytes = 40u * 1024u;
    static constexpr float kFullScale = 2.0f;        // stored range is +-kFullScale

    // Bytes to allocate for a loop buffer: `wanted` when the heap can spare it, else what
    // is left after `reserve`, rounded down to whole pairs; 0 when that is too little.
    static size_t planBufferBytes(size_t freeHeapBytes, size_t reserveBytes,
                                  size_t wantedBytes) noexcept;

    // 12-bit pack/unpack, public so the storage format has one definition and one test.
    // Sample `index` of a buffer of pairs; values are signed -2048..2047.
    static int16_t unpack(const uint8_t *buffer, uint32_t index) noexcept;
    static void pack(uint8_t *buffer, uint32_t index, int value) noexcept;

    // --- Setup (control thread, before the audio thread runs) ---------------------
    // The engine does not own the memory. `bytes` is rounded down to whole pairs.
    void attach(uint8_t *storage, size_t bytes, float sampleRate) noexcept;
    uint32_t capacitySamples() const noexcept { return capacity_; }

    // --- Control thread (Core 0) --------------------------------------------------
    // Setters are lock-free targets; the audio thread eases toward them.
    void setLoopVolume(float volume) noexcept;      // 0..1
    void setSequencerVolume(float volume) noexcept; // 0..1: the live bus level in the mix
    void setRegen(float regen) noexcept;            // kMinRegen..1: loop level kept per repeat
    float loopVolume() const noexcept { return loopVolume_.load(std::memory_order_relaxed); }
    float sequencerVolume() const noexcept { return seqVolume_.load(std::memory_order_relaxed); }
    float regen() const noexcept { return regen_.load(std::memory_order_relaxed); }

    // Frames one pass of the playing loop should last at the current tempo. Idempotent:
    // publish it whenever the tempo or the loop size changes.
    void setPeriodFrames(uint32_t frames) noexcept
    {
        periodFrames_.store(frames, std::memory_order_relaxed);
    }

    // Commands cross to the audio thread through a small ring; each returns false when
    // the ring is full (retry on the next pass). Only one thread may call these.
    bool postRecord(uint32_t periodFrames) noexcept; // start a take now: record, layer over a playing loop, or queue the layer after the running pass
    bool postCancel() noexcept;                      // drop an unfinished first take
    bool postClear() noexcept;                       // stop and forget the loop
    bool postSync() noexcept;                        // the step clock reached a loop boundary
    bool postRestart() noexcept;                     // the transport restarted: loop from the top

    // Published by the audio thread once per block.
    State state() const noexcept
    {
        return static_cast<State>(publishedState_.load(std::memory_order_relaxed));
    }
    float position() const noexcept; // 0..1 through the loop
    // How many takes the audio thread has begun. A take posted by Core 0 is acknowledged
    // once this passes the count taken before posting.
    uint32_t takesStarted() const noexcept { return takes_.load(std::memory_order_relaxed); }

    static constexpr float kMinRegen = LoopTiming::kMinRegen;

    // --- Audio thread (Core 1) ----------------------------------------------------
    // `mix` holds the mono bus after the delay and before the reverb. On return it holds
    //   mix * sequencerVolume + loop * loopVolume
    // and the recording tap saw `mix` before the sequencer volume was applied, so turning
    // the sequencer down to hear the loop never records quieter. With nothing recorded and
    // the sequencer volume at unity the buffer is left untouched, bit for bit.
    void processBlock(float *mix, uint32_t n) noexcept;

    // Host-test observability (audio thread state; read it only while no block runs).
    uint32_t storedSamples() const noexcept { return count_; }
    float passGain() const noexcept { return passGain_; }
    State audioState() const noexcept { return state_; }

private:
    struct Command
    {
        uint8_t type = 0;
        uint8_t reserved[3] = {};
        uint32_t value = 0;
    };
    enum CommandType : uint8_t { kRecord = 1, kCancel, kClear, kSync, kRestart };
    static constexpr size_t kCommandCapacity = 8;

    static constexpr uint32_t kSpan = 32;            // commands and gains are applied per span
    static constexpr float kGainTauSeconds = 0.015f; // zipper-free fader moves
    static constexpr float kEdgeFadeSeconds = 0.001f; // seam fade: no click at the wrap
    static constexpr uint32_t kSnapFadeFrames = 64;  // crossfade when a sync really moves the head
    static constexpr uint32_t kSyncToleranceFrames = 144; // 3 ms at 48 kHz: leave jitter alone
    static constexpr float kMinPassGain = 1.0f / 1024.0f; // below -60 dB the loop has faded out
    static constexpr float kReconMaxRate = 0.9f;     // above this stored/frame ratio, no smoothing

    bool post_(CommandType type, uint32_t value) noexcept;
    void renderSpan_(float *mix, uint32_t n) noexcept;
    void drainCommands_() noexcept;
    void beginTake_(uint32_t periodFrames) noexcept;
    void setPeriod_(uint32_t periodFrames) noexcept;
    void snapToStart_() noexcept;
    void advance_() noexcept;
    void onWrap_() noexcept;
    void bake_(uint32_t index) noexcept;
    void loadCache_() noexcept;
    float readAt_(uint64_t position) const noexcept;
    void publish_() noexcept;

    // --- audio-thread state ---
    uint8_t *store_ = nullptr;
    uint32_t capacity_ = 0;     // samples the buffer can hold (even)
    float sampleRate_ = 48000.0f;
    float gainAlphaSpan_ = 0.04f;
    State state_ = State::Disabled;
    uint32_t count_ = 0;        // stored samples in the loop (<= capacity_)
    uint64_t pos_ = 0;          // Q32.32 position, in stored samples
    uint64_t incr_ = 0;         // Q32.32 stored samples per frame
    uint64_t end_ = 0;          // count_ in Q32.32
    uint32_t index_ = 0;        // pos_ >> 32
    float cur_ = 0.0f;          // sample at index_ (the OLD value until it is baked)
    float next_ = 0.0f;         // sample at index_ + 1, wrapping
    float acc_ = 0.0f;          // live bus summed over the frames spent on index_
    uint32_t accCount_ = 0;
    float passGain_ = 1.0f;     // regen applied so far this life of the loop
    float regenNow_ = 1.0f;     // regen as read for this span
    uint32_t appliedPeriod_ = 0;
    bool chainDub_ = false;     // a layer is queued behind the pass that is running
    bool justBaked_ = false;    // the pass in progress is the first after a take or layer: its unity gain was
                                // set by that wrap, not decayed by it
    uint32_t edgeSamples_ = 1;  // stored samples in the seam fade
    float edgeInv_ = 1.0f;
    uint64_t fadePos_ = 0;      // the pre-snap head, still fading out
    uint32_t fadeLeft_ = 0;
    float seqGain_ = 1.0f;
    float loopGain_ = 1.0f;
    float reconAlpha_ = 1.0f;   // two-pole smoothing of decimated playback
    float recon1_ = 0.0f;
    float recon2_ = 0.0f;
    bool reconOn_ = false;

    // --- control -> audio ---
    std::atomic<float> loopVolume_{1.0f};
    std::atomic<float> seqVolume_{1.0f};
    std::atomic<float> regen_{1.0f};
    std::atomic<uint32_t> periodFrames_{0};
    SpscQueue<Command, kCommandCapacity> commands_;
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<uint32_t>::is_always_lock_free,
                  "Loop controls must be lock-free");

    // --- audio -> control ---
    std::atomic<uint8_t> publishedState_{static_cast<uint8_t>(State::Disabled)};
    std::atomic<uint32_t> publishedPosition_{0}; // Q16 fraction of the loop
    std::atomic<uint32_t> takes_{0};
};
