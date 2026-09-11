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
    hand.observeDistance(lo); REQUIRE(hand.recordingValue()==0.0f);
    hand.observeDistance(hi); REQUIRE(hand.recordingValue()==1.0f);
    hand.observeDistance((lo+hi)/2);
    REQUIRE(hand.recordingValue()==Catch::Approx(0.5f).margin(1.0f/(hi-lo)));
    for(int invalid:{-1,0,lo-1,hi+1,8191}) {
        hand.observeDistance(invalid);
        REQUIRE(hand.recordingValue()==0.0f);
    }
}
