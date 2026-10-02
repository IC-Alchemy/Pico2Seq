#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "src/app/AppState.h"
#include "src/app/Pcm16.h"
#include <array>

TEST_CASE("Every signed PCM16 level survives the DAC conversion", "[app][pcm]")
{
    for (int32_t pcm = INT16_MIN; pcm <= INT16_MAX; ++pcm)
    {
        const float sample = static_cast<float>(pcm) / 32768.0f;
        REQUIRE(AudioSamples::toPcm16(sample) == pcm);
    }
}

TEST_CASE("DAC conversion clips peaks and truncates quiet samples", "[app][pcm]")
{
    REQUIRE(AudioSamples::toPcm16(1.0f) == INT16_MAX);
    REQUIRE(AudioSamples::toPcm16(8.0f) == INT16_MAX);
    REQUIRE(AudioSamples::toPcm16(-8.0f) == INT16_MIN);
    REQUIRE(AudioSamples::toPcm16(0.0f) == 0);
    REQUIRE(AudioSamples::toPcm16(0.75f / 32768.0f) == 0);
    REQUIRE(AudioSamples::toPcm16(-0.75f / 32768.0f) == 0);
    REQUIRE(AudioSamples::toPcm16(1.75f / 32768.0f) == 1);
    REQUIRE(AudioSamples::toPcm16(-1.75f / 32768.0f) == -1);
}

TEST_CASE("Stereo PCM16 conversion keeps the channels separate and clips each on its own", "[app][pcm][stereo]")
{
    // Distinct ramps on the two channels, with peaks past full scale on either side.
    constexpr uint32_t frames = 1024;
    std::array<float, frames> left{}, right{};
    for (uint32_t i = 0; i < frames; ++i)
    {
        left[i] = 2.0f * static_cast<float>(i) / frames - 1.0f;     // -1 .. +1
        right[i] = -3.0f * static_cast<float>(i) / frames + 1.5f;   // +1.5 .. -1.5 (clips both ends)
    }
    std::array<int16_t, 2 * frames + 2> out{};
    out[2 * frames] = 0x1234;
    out[2 * frames + 1] = 0x5678;
    AudioSamples::interleavePcm16(left.data(), right.data(), out.data(), frames);
    for (uint32_t i = 0; i < frames; ++i)
    {
        REQUIRE(out[2 * i] == AudioSamples::toPcm16(left[i]));
        REQUIRE(out[2 * i + 1] == AudioSamples::toPcm16(right[i]));
    }
    REQUIRE(out[1] == INT16_MAX);            // right starts above full scale
    REQUIRE(out[2 * (frames - 1) + 1] == INT16_MIN); // ...and ends below it
    REQUIRE(out[2 * frames] == 0x1234);      // nothing written past the last frame
    REQUIRE(out[2 * frames + 1] == 0x5678);

    // Hard-panned and opposite-polarity images survive intact.
    std::array<float, 4> hardLeft{0.5f, -0.5f, 0.25f, 0.0f}, silent{};
    std::array<int16_t, 8> panned{};
    AudioSamples::interleavePcm16(hardLeft.data(), silent.data(), panned.data(), 4);
    for (uint32_t i = 0; i < 4; ++i)
    {
        REQUIRE(panned[2 * i] == AudioSamples::toPcm16(hardLeft[i]));
        REQUIRE(panned[2 * i + 1] == 0);
    }
    std::array<float, 3> positive{0.5f, 0.5f, 0.5f}, negative{-0.5f, -0.5f, -0.5f};
    std::array<int16_t, 6> inverted{};
    AudioSamples::interleavePcm16(positive.data(), negative.data(), inverted.data(), 3);
    for (uint32_t i = 0; i < 3; ++i)
        REQUIRE(inverted[2 * i] == -inverted[2 * i + 1]);

    // Zero frames write nothing.
    std::array<int16_t, 2> untouched{42, 43};
    AudioSamples::interleavePcm16(nullptr, nullptr, untouched.data(), 0);
    REQUIRE(untouched[0] == 42);
    REQUIRE(untouched[1] == 43);
}

TEST_CASE("Hand modifiers use the whole calibrated lidar range", "[app][recording]")
{
    AppState::PerformanceInput hand;
    constexpr int lo=SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
    constexpr int hi=SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
    hand.observeDistance(lo); REQUIRE(hand.handPresent); REQUIRE(hand.recordingValue()==0.0f);
    hand.observeDistance(hi); REQUIRE(hand.handPresent); REQUIRE(hand.recordingValue()==1.0f);
    hand.observeDistance((lo+hi)/2);
    REQUIRE(hand.handPresent);
    REQUIRE(hand.recordingValue()==Catch::Approx(0.5f).margin(1.0f/(hi-lo)));
}

TEST_CASE("Readings just outside the lidar window clamp to its edges", "[app][recording]")
{
    AppState::PerformanceInput hand;
    constexpr int lo=SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
    constexpr int hi=SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
    constexpr int tolerance=SensorConstants::DistanceSensor::EDGE_TOLERANCE_MM;
    for(int nearEdge:{lo-1,lo-tolerance}) {
        hand.observeDistance(nearEdge);
        REQUIRE(hand.handPresent);
        REQUIRE(hand.recordingValue()==0.0f);
    }
    for(int farEdge:{hi+1,hi+tolerance}) {
        hand.observeDistance(farEdge);
        REQUIRE(hand.handPresent);
        REQUIRE(hand.recordingValue()==1.0f);
    }
}

TEST_CASE("No hand in range is absent, not a minimum-distance hand", "[app][recording]")
{
    // An absent hand used to read as the window minimum, recording a -50%
    // modifier into every step it passed and showing the parameter at 0%.
    AppState::PerformanceInput hand;
    constexpr int lo=SensorConstants::DistanceSensor::MIN_DISTANCE_HEIGHT_MM;
    constexpr int hi=SensorConstants::DistanceSensor::MAX_DISTANCE_HEIGHT_MM;
    constexpr int tolerance=SensorConstants::DistanceSensor::EDGE_TOLERANCE_MM;
    for(int absent:{SensorConstants::DistanceSensor::INVALID_DISTANCE_MM,0,lo-tolerance-1,
                    hi+tolerance+1,2400,8191}) {
        CAPTURE(absent);
        hand.observeDistance((lo+hi)/2);
        REQUIRE(hand.handPresent);
        hand.observeDistance(absent);
        REQUIRE_FALSE(hand.handPresent);
        REQUIRE(hand.distanceAboveMinimumMm==0);
    }
}
