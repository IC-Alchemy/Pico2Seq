#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "src/app/AppState.h"
#include "src/app/Pcm16.h"

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
