#include <catch2/catch_test_macros.hpp>
#include "src/app/SequencerView.h"
#include "src/pico2seq-core/sequencer/Sequencer.h"
#include <limits>
#include <type_traits>

static_assert(!std::is_constructible_v<SequencerView, Sequencer *const (&)[3]>);
static_assert(!std::is_constructible_v<SequencerView, Sequencer *const (&)[5]>);

TEST_CASE("Sequencer view routes all four voices without copying state", "[app][sequencer_view]")
{
    Sequencer first(1), second(2), third(3), fourth(4);
    Sequencer *const routing[VoiceSystem::MAX_VOICES] = {&first, &second, &third, &fourth};
    const SequencerView view{routing};
    const auto copy = view;

    REQUIRE(view.size() == 4);
    REQUIRE(view.data() == routing);
    REQUIRE(copy.data() == routing);
    for (std::size_t voice = 0; voice < view.size(); ++voice)
    {
        CAPTURE(voice);
        REQUIRE(view.get(voice) == routing[voice]);
        REQUIRE(&view.clamped(voice) == routing[voice]);
        view.get(voice)->setParameterStepCount(ParamId::Gate, static_cast<uint8_t>(voice + 2));
    }
    for (std::size_t voice = 0; voice < view.size(); ++voice)
        REQUIRE(copy.clamped(voice).getParameterStepCount(ParamId::Gate) == voice + 2);
}

TEST_CASE("Sequencer view rejects invalid edits and preserves last-voice display fallback", "[app][sequencer_view]")
{
    Sequencer first(1), second(2), third(3), fourth(4);
    Sequencer *const routing[VoiceSystem::MAX_VOICES] = {&first, &second, &third, &fourth};
    const SequencerView view{routing};

    for (const std::size_t invalid : {view.size(), std::size_t{255}, std::numeric_limits<std::size_t>::max()})
    {
        CAPTURE(invalid);
        REQUIRE(view.get(invalid) == nullptr);
        REQUIRE(&view.clamped(invalid) == &fourth);
    }
}
