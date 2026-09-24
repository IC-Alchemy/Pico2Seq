#include "SitarInstrument.h"

#include <algorithm>
#include <cmath>

// SitarInstrument.cpp — the audio half of Sitar Explorer.
// Core 0 (control) queues gestures and writes lane targets; Core 1 (audio)
// drains, applies and renders. Nothing here allocates, locks or logs, and the
// four courses live in static storage at the bottom of the file — the object is
// ~50 KB, far past any stack budget on either core.

namespace Sitar
{
namespace
{
// Program-long instance (see instrument()). Static so the four delay lines are
// in .bss rather than on a stack: 50,880 bytes of course state.
Instrument g_instrument;
} // namespace

Instrument &instrument() noexcept
{
    return g_instrument;
}

void Instrument::prepare(float sampleRate) noexcept
{
    const float rate = sampleRate > 1000.0f ? sampleRate : 48000.0f;
    sampleRate_ = rate;
    melody_.prepare(rate);
    chikariPa_.prepare(rate);
    chikariSa_.prepare(rate);
    kharaj_.prepare(rate);
    for (uint8_t index = 0; index < kParamCount; ++index)
    {
        targets_[index].store(0.0f, std::memory_order_relaxed);
        applied_[index] = 0.0f;
    }
    for (uint8_t course = 0; course < kCourseCount; ++course)
    {
        envelope_[course] = 0.0f;
        published_[course].store(0.0f, std::memory_order_relaxed);
    }
    // The lanes start at sitar.h's own defaults, expressed as the normalized
    // travel the UI speaks: 0 would be a very short, dark string.
    for (uint8_t index = 0; index < kParamCount; ++index)
    {
        const auto id = static_cast<Param>(index);
        const float normalized = normalizedValue(id, paramInfo(id).defaultValue);
        targets_[index].store(normalized, std::memory_order_relaxed);
    }
    applyChangedParameters();
    prepared_ = true;
}

bool Instrument::pluck(Course course, float frequencyHz, float amplitude) noexcept
{
    Event event;
    event.kind = Event::Kind::Pluck;
    event.course = course;
    event.frequency = frequencyHz;
    event.amplitude = amplitude;
    return events_.tryPush(event);
}

bool Instrument::slide(Course course, float frequencyHz) noexcept
{
    Event event;
    event.kind = Event::Kind::Slide;
    event.course = course;
    event.frequency = frequencyHz;
    return events_.tryPush(event);
}

bool Instrument::damp(Course course) noexcept
{
    Event event;
    event.kind = Event::Kind::Damp;
    event.course = course;
    return events_.tryPush(event);
}

void Instrument::dampAll() noexcept
{
    for (uint8_t course = 0; course < kCourseCount; ++course)
        damp(static_cast<Course>(course));
}

void Instrument::setParameter(Param id, float normalized) noexcept
{
    const float travel = normalized > 1.0f ? 1.0f : (normalized > 0.0f ? normalized : 0.0f);
    targets_[paramIndex(id) < kParamCount ? paramIndex(id) : 0].store(travel, std::memory_order_relaxed);
}

float Instrument::parameter(Param id) const noexcept
{
    return targets_[paramIndex(id) < kParamCount ? paramIndex(id) : 0].load(std::memory_order_relaxed);
}

float Instrument::courseLevel(Course course) const noexcept
{
    const uint8_t index = static_cast<uint8_t>(course);
    if (index >= kCourseCount)
        return 0.0f;
    return published_[index].load(std::memory_order_relaxed);
}

void Instrument::applyChangedParameters() noexcept
{
    for (uint8_t index = 0; index < kParamCount; ++index)
    {
        const float target = targets_[index].load(std::memory_order_relaxed);
        if (target == applied_[index])
            continue;
        applied_[index] = target;
        const auto id = static_cast<Param>(index);
        const float engineering = engineeringValue(id, target);
        applyOne(melody_, id, engineering);
        applyOne(chikariPa_, id, engineering);
        applyOne(chikariSa_, id, engineering);
        applyOne(kharaj_, id, engineering);
    }
}

void Instrument::renderAdd(float *out, uint32_t count) noexcept
{
    if (!prepared_)
        // Core 1 can start before Core 0 has prepared the courses; stay silent
        // rather than running an unprepared delay line.
        return;

    applyChangedParameters();

    Event event;
    for (uint8_t drained = 0; drained < kMaxEventsPerBlock && events_.tryPop(event); ++drained)
    {
        switch (event.course)
        {
        case Course::Main: applyEvent(melody_, event); break;
        case Course::ChikariLow: applyEvent(chikariPa_, event); break;
        case Course::ChikariHigh: applyEvent(chikariSa_, event); break;
        case Course::Kharaj: applyEvent(kharaj_, event); break;
        case Course::Count: break;
        }
    }

    float peak[kCourseCount] = {};
    constexpr uint8_t kMain = static_cast<uint8_t>(Course::Main);
    constexpr uint8_t kChikariLow = static_cast<uint8_t>(Course::ChikariLow);
    constexpr uint8_t kChikariHigh = static_cast<uint8_t>(Course::ChikariHigh);
    constexpr uint8_t kKharaj = static_cast<uint8_t>(Course::Kharaj);
    for (uint32_t i = 0; i < count; ++i)
    {
        // Each course returns 0 immediately while it is silent, so an idle
        // sitar costs four comparisons per sample.
        const float melody = melody_.process();
        const float chikariPa = chikariPa_.process();
        const float chikariSa = chikariSa_.process();
        const float kharaj = kharaj_.process();
        out[i] += (melody + chikariPa + chikariSa + kharaj) * kOutputGain;
        // Peak per block is all the LED bloom and OLED meter need: fades are
        // the light layer's job, and a per-sample follower would cost more than
        // the strings themselves.
        peak[kMain] = std::max(peak[kMain], std::fabs(melody));
        peak[kChikariLow] = std::max(peak[kChikariLow], std::fabs(chikariPa));
        peak[kChikariHigh] = std::max(peak[kChikariHigh], std::fabs(chikariSa));
        peak[kKharaj] = std::max(peak[kKharaj], std::fabs(kharaj));
    }

    // Publish a decaying envelope: 12 dB/s of release, so a pluck flashes and a
    // taraf tail breathes down over roughly a second.
    constexpr float kRelease = 0.94f;
    for (uint8_t course = 0; course < kCourseCount; ++course)
    {
        const float level = std::min(1.0f, std::max(peak[course], envelope_[course] * kRelease));
        envelope_[course] = level;
        published_[course].store(level, std::memory_order_relaxed);
    }
}

} // namespace Sitar
