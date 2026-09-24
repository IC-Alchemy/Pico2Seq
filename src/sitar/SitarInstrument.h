#pragma once

#include "SitarControls.h"
#include "SitarParameters.h"
#include "../utils/SpscQueue.h"
#include "../voice/VoiceManager.h"
#include "../rpdsp/src/rpdsp/sitar.h"

#include <atomic>
#include <cstdint>

// SitarInstrument.h — the audio half of Sitar Explorer (crosses Core 0 -> Core 1).
// Musical role: the box stops being four sequencer voices for a moment and
// becomes a sitar. Four string courses ring at once — the melody string every
// meend bends, the two chikari drone strings, and the kharaj bass string — each
// one an independent `rpdsp::SitarStringVoice` so plucking the drone can never
// retune the note still singing on the melody string.
// Technical role: the only sitar object allowed to hold applied DSP state. Core 0
// publishes events through a lock-free queue and lane targets through lock-free
// atomics; Core 1 drains the queue, applies changed lanes, and sums the courses
// into the master bus through `VoiceManager`'s auxiliary-instrument hook (so the
// master fader, delay and compressor hear the sitar like any other source).
namespace Sitar
{

// String courses, sized to the pitches they carry: the melody and bass strings
// need the long delay lines, the chikari pair only needs enough for ~95 Hz.
constexpr size_t kMainCapacity = 2048;
constexpr size_t kDroneCapacity = 1024;

using MainVoice = rpdsp::SitarStringVoice<kMainCapacity>;
using DroneVoice = rpdsp::SitarStringVoice<kDroneCapacity>;

// One queued gesture. Events are ordered, so a pluck followed by a meend always
// arrives as a pluck followed by a meend.
struct Event
{
    enum class Kind : uint8_t { Pluck = 0, Slide, Damp };
    Kind kind = Kind::Pluck;
    Course course = Course::Main;
    float frequency = 0.0f;
    float amplitude = 1.0f;
};

class Instrument : public VoiceManager::AuxiliaryInstrument
{
public:
    // Deep enough for a jod sweep plus four courses of drone strokes; a full
    // queue drops the newest gesture instead of stalling Core 0.
    static constexpr size_t kEventCapacity = 64;
    // Bounded drain per block: a backlog must never eat a render slice
    // (5.33 ms at 48 kHz / 256 frames).
    static constexpr uint8_t kMaxEventsPerBlock = 16;
    // Headroom for four courses ringing together; the master compressor and
    // Pcm16's clamp stay the ceiling.
    static constexpr float kOutputGain = 0.3f;

    // Control thread (Core 0 setup): prepare the courses and land the lane
    // targets on the current values. Mirrors VoiceManager::init's boot order.
    void prepare(float sampleRate) noexcept;

    // --- Control thread (Core 0) ---
    // All three are non-blocking: false means the queue was full and the
    // gesture was dropped (still never a stall on the control core).
    bool pluck(Course course, float frequencyHz, float amplitude) noexcept;
    bool slide(Course course, float frequencyHz) noexcept;
    bool damp(Course course) noexcept;
    void dampAll() noexcept;
    // Lane target, normalized 0..1 as the whole UI speaks. Latest value wins;
    // the audio thread applies whatever changed at its next block.
    void setParameter(Param id, float normalized) noexcept;
    float parameter(Param id) const noexcept;

    // --- Audio thread (Core 1) ---
    void renderAdd(float *out, uint32_t count) noexcept override;

    // --- Read by Core 0 for feedback (published by the audio thread) ---
    // 0..1 decaying envelope of each course, for the LED bloom and the OLED
    // meter. Reading never touches DSP state.
    float courseLevel(Course course) const noexcept;

private:
    void applyChangedParameters() noexcept;

    // One queued gesture into one course. A template because the courses are
    // different SitarStringVoice instantiations (different delay-line
    // capacities) and therefore different types.
    template <typename Voice>
    void applyEvent(Voice &voice, const Event &event) noexcept
    {
        switch (event.kind)
        {
        case Event::Kind::Pluck: voice.pluck(event.frequency, event.amplitude); break;
        case Event::Kind::Slide: voice.slideTo(event.frequency); break;
        case Event::Kind::Damp: voice.reset(); break;
        }
    }

    template <typename Voice>
    void applyOne(Voice &voice, Param id, float value) noexcept
    {
        switch (id)
        {
        case Param::StringT60: voice.setDecayTimeSeconds(value); break;
        case Param::Brightness: voice.setBrightness(value); break;
        case Param::PickPosition: voice.setPickPosition(value); break;
        case Param::PickHardness: voice.setPickHardness(value); break;
        case Param::Stiffness: voice.setStiffness(value); break;
        case Param::Detune: voice.setDetuneCents(value); break;
        case Param::Jawari: voice.setJawari(value); break;
        case Param::JawariContact: voice.setJawariThreshold(value); break;
        case Param::TarafAmount: voice.setTarafAmount(value); break;
        case Param::TarafRing: voice.setTarafDecaySeconds(value); break;
        case Param::BodyAmount: voice.setBodyAmount(value); break;
        case Param::BodyTone: voice.setBodyFrequency(value); break;
        case Param::Meend: voice.setSlideTimeSeconds(value); break;
        case Param::Count: break;
        }
    }

    MainVoice melody_;
    DroneVoice chikariPa_;
    DroneVoice chikariSa_;
    MainVoice kharaj_;

    SpscQueue<Event, kEventCapacity> events_;

    // Targets are written by Core 0 and read by Core 1; applied_ is the audio
    // thread's cache so a lane is pushed into the DSP only when it moves.
    std::atomic<float> targets_[kParamCount];
    static_assert(std::atomic<float>::is_always_lock_free, "Lane targets must be lock-free");
    float applied_[kParamCount];

    // Envelope follower state and its published mirror.
    float envelope_[kCourseCount];
    std::atomic<float> published_[kCourseCount];

    float sampleRate_ = 48000.0f;
    bool prepared_ = false;
};

// Program-long instance: the firmware's audio path and the mode's glue share
// this one object (same pattern as voiceManager in AppState).
Instrument &instrument() noexcept;

} // namespace Sitar
