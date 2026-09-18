#pragma once

#include "../voice/VoiceSystem.h"
#include <cstddef>

class Sequencer;

// Non-owning view of the fixed voice routing table. The table and its non-null
// sequencers must outlive the view; copying a view never copies sequencer state.
class SequencerView
{
public:
    constexpr explicit SequencerView(Sequencer *const (&sequencers)[VoiceSystem::MAX_VOICES]) noexcept
        : sequencers_(sequencers) {}

    constexpr std::size_t size() const noexcept { return VoiceSystem::MAX_VOICES; }
    constexpr Sequencer *const *data() const noexcept { return sequencers_; }

    // Control actions must reject invalid voice indices, not edit another voice.
    constexpr Sequencer *get(std::size_t voice) const noexcept
    {
        return voice < size() ? sequencers_[voice] : nullptr;
    }

    // Renderers historically display the last voice for an invalid selection.
    Sequencer &clamped(std::size_t voice) const noexcept
    {
        return *sequencers_[voice < size() ? voice : size() - 1];
    }

private:
    Sequencer *const (&sequencers_)[VoiceSystem::MAX_VOICES];
};
