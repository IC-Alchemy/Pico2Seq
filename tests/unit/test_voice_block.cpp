#include <catch2/catch_test_macros.hpp>
#include "voice/Voice.h"
#include "voice/VoiceManager.h"
#include "voice/VoicePresets.h"
#include "voice/VoiceEditParameters.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
VoiceConfig patch(uint8_t preset)
{
    auto config = VoicePresets::getPresetConfig(preset);
    VoiceEdit::enablePatch(config);
    return config;
}

VoiceState note(float index, bool gate = true, bool slide = false)
{
    VoiceState state;
    state.noteIndex = index;
    state.isGateHigh = gate;
    state.hasSlide = slide;
    state.shouldRetrigger = gate && !slide;
    return state;
}

constexpr std::array<uint32_t, 7> chunks{1, 7, 32, 33, 64, 3, 256};

void compare(Voice &scalar, Voice &block, uint32_t samples, float tolerance)
{
    std::array<float, 256> output{};
    uint32_t chunk = 0;
    float maxError = 0.0f;
    bool finite = true;
    while (samples)
    {
        const auto n = std::min(samples, chunks[chunk++ % chunks.size()]);
        block.processBlock(output.data(), n);
        for (uint32_t k = 0; k < n; ++k)
        {
            const float expected = scalar.process();
            finite &= std::isfinite(output[k]) && std::isfinite(expected);
            maxError = std::max(maxError, std::fabs(output[k] - expected));
        }
        samples -= n;
    }
    CAPTURE(maxError, tolerance);
    REQUIRE(finite);
    REQUIRE(maxError <= tolerance);
}
}

TEST_CASE("processBlock matches per-sample process() for every preset", "[voice][voice_block]")
{
    for (uint8_t preset = 0; preset < VoicePresets::getPresetCount(); ++preset)
    {
        CAPTURE(preset);
        Voice scalar(2, patch(preset)), block(2, patch(preset));
        scalar.init(48000);
        block.init(48000);
        auto update = [&](const VoiceState &state) {
            scalar.updateParameters(state);
            block.updateParameters(state);
        };
        update(note(24));
        compare(scalar, block, 14400, 0.0f);
        update(note(29, true, true));
        compare(scalar, block, 2400, 0.0f);
        update(note(31, true, true));
        update(note(34, true, true));
        compare(scalar, block, 2400, 0.0f);
        update(note(34, false));
        compare(scalar, block, 48000, 1.0e-6f);
        const auto next = patch((preset + 5) % VoicePresets::getPresetCount());
        scalar.setConfig(next);
        block.setConfig(next);
        update(note(27));
        compare(scalar, block, 14400, 1.0e-6f);
        // A gated structural change is applied at the falling edge.
        scalar.setConfig(patch(preset));
        block.setConfig(patch(preset));
        compare(scalar, block, 33, 1.0e-6f);
        update(note(27, false));
        compare(scalar, block, 256, 1.0e-6f);
    }
}

TEST_CASE("Queued gate edges each reach a sample inside one block", "[voice][voice_block]")
{
    Voice scalar(1, patch(4)), block(1, patch(4));
    scalar.init(48000);
    block.init(48000);
    for (const auto &state : {note(4), note(5, false), note(6)})
    {
        scalar.updateParameters(state);
        block.updateParameters(state);
    }
    std::array<float, 3> output{};
    block.processBlock(output.data(), output.size());
    for (float sample : output) REQUIRE(sample == scalar.process());
    REQUIRE(block.getState().noteIndex == 6);
    REQUIRE(block.getGate());
}

TEST_CASE("Block rendering drains disabled voices and leaves zero-length calls alone", "[voice][voice_block]")
{
    Voice voice(1, patch(4));
    voice.init(48000);
    voice.setEnabled(false);
    voice.setFilterFrequency(2300);
    voice.setEnabled(true);
    voice.updateParameters(note(12));
    voice.processBlock(nullptr, 0);
    REQUIRE_FALSE(voice.getGate());
    std::array<float, 5> output{99, 99, 99, 99, 99};
    voice.processBlock(output.data(), 4);
    REQUIRE(output[0] == 0);
    REQUIRE(output[1] == 0);
    REQUIRE(output[4] == 99);
    REQUIRE(voice.getGate());
    REQUIRE(voice.getConfig().enabled);
    REQUIRE(voice.getState().noteIndex == 12);
}

TEST_CASE("VoiceManager::processBlock matches processAllVoices()", "[voice][voice_block]")
{
    VoiceManager scalar(4), block(4);
    std::array<uint8_t, 4> ids{};
    const std::array<uint8_t, 4> presets{4, 2, 1, 6};
    for (uint8_t i = 0; i < 4; ++i)
    {
        ids[i] = scalar.addVoice(patch(presets[i]));
        REQUIRE(block.addVoice(patch(presets[i])) == ids[i]);
        scalar.updateVoiceState(ids[i], note(24 + i * 3));
        block.updateVoiceState(ids[i], note(24 + i * 3));
    }
    std::array<float, 514> output{};
    block.processBlock(nullptr, 0);
    for (uint32_t call = 0; call < 140; ++call)
    {
        if (call == 8) { scalar.setGlobalVolume(0.23f); block.setGlobalVolume(0.23f); }
        if (call == 16) { scalar.setTransportMuted(true); block.setTransportMuted(true); }
        if (call == 24) { scalar.setTransportMuted(false); block.setTransportMuted(false); }
        if (call == 40)
            for (auto id : ids)
            {
                scalar.updateVoiceState(id, note(24, false));
                block.updateVoiceState(id, note(24, false));
            }
        // Includes calls larger than the manager scratch allocation.
        const uint32_t n = call % 5 == 0 ? 513 : chunks[call % chunks.size()];
        output[n] = 99.0f;
        block.processBlock(output.data(), n);
        for (uint32_t k = 0; k < n; ++k)
        {
            const float expected = scalar.processAllVoices();
            CAPTURE(call, k);
            REQUIRE(std::fabs(output[k] - expected) <= (call < 40 ? 0.0f : 1.0e-6f));
        }
        REQUIRE(output[n] == 99.0f);
    }
}
