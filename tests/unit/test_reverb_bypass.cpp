#include <catch2/catch_test_macros.hpp>
#include "voice/MasterReverb.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

// This executable is built with -DPICO2SEQ_REVERB_BYPASS=1 (tests/CMakeLists.txt):
// the reverb-free baseline for the on-board CPU A/B. The tank never runs, so the
// bus is dry whatever the controls say, and the control hand-off still works.

static_assert(MasterReverb::kBypass, "this target must be built with PICO2SEQ_REVERB_BYPASS=1");

namespace
{
std::vector<float> testSignal(size_t frames)
{
    std::vector<float> dry(frames);
    for (size_t i = 0; i < frames; ++i)
        dry[i] = 0.5f * std::sin(0.05f * static_cast<float>(i)) + 0.1f * std::sin(0.71f * static_cast<float>(i));
    return dry;
}
} // namespace

TEST_CASE("a bypass build renders the dry bus on both channels", "[reverb_bypass]")
{
    auto reverb = std::make_unique<MasterReverb>();
    // Controls at their extremes: none of it may reach the output.
    reverb->setMix(1.0f);
    reverb->setDecaySeconds(1000.0f);
    reverb->setWidth(2.0f);
    reverb->setFreeze(true);

    const std::vector<float> dry = testSignal(300);
    std::vector<float> left(dry.size(), 7.0f);
    std::vector<float> right(dry.size(), -7.0f);
    // Irregular calls, as the bus makes them.
    size_t done = 0;
    for (size_t chunk : {1u, 63u, 64u, 65u, 107u})
    {
        reverb->render(dry.data() + done, left.data() + done, right.data() + done,
                       static_cast<uint32_t>(chunk));
        done += chunk;
    }
    REQUIRE(done == dry.size());
    CHECK(std::memcmp(left.data(), dry.data(), dry.size() * sizeof(float)) == 0);
    CHECK(std::memcmp(right.data(), dry.data(), dry.size() * sizeof(float)) == 0);

    // Zero frames touch nothing.
    left[0] = 3.0f;
    reverb->render(dry.data(), left.data(), right.data(), 0);
    CHECK(left[0] == 3.0f);
}

TEST_CASE("a bypass build keeps the control hand-off and names itself", "[reverb_bypass]")
{
    auto reverb = std::make_unique<MasterReverb>();
    reverb->setMix(0.4f);
    reverb->setDecaySeconds(6.0f);
    CHECK(reverb->settings().mix == 0.4f);
    CHECK(reverb->settings().decaySeconds == 6.0f);
    CHECK(std::strcmp(MasterReverb::kVariantName, "bypass") == 0);
}
