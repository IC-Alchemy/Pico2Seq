#pragma once

// SequencerView: the fixed 4-voice routing table as the performer sees it (1-4).
// Musical role: guarantees a pad/knob gesture always lands on the intended voice.
// Non-owning view: the table and sequencers must outlive it; copying never
// copies pattern state. Core 0 only.

#include "../voice/VoiceSystem.h"
#include <cstddef>

class Sequencer;

// Non-owning view of the fixed voice routing table (see above).
class SequencerView
{
public:
    constexpr explicit SequencerView(Sequencer *const (&sequencers)[VoiceSystem::MAX_VOICES]) noexcept
        : sequencers_(sequencers) {}

    constexpr std::size_t size() const noexcept { return VoiceSystem::MAX_VOICES; }
    constexpr Sequencer *const *data() const noexcept { return sequencers_; }

    // Invalid index: reject (nullptr) so controls never edit the wrong voice.
    constexpr Sequencer *get(std::size_t voice) const noexcept
    {
        return voice < size() ? sequencers_[voice] : nullptr;
    }

    // Renderers show the last voice for an invalid selection (legacy behavior).
    Sequencer &clamped(std::size_t voice) const noexcept
    {
        return *sequencers_[voice < size() ? voice : size() - 1];
    }

private:
    Sequencer *const (&sequencers_)[VoiceSystem::MAX_VOICES];
};
