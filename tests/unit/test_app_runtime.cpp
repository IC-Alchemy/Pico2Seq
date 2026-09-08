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

TEST_CASE("Hand recording keeps the existing distance calibration", "[app][recording]")
{
    AppState::PerformanceInput hand;
    REQUIRE(hand.distanceAboveMinimumMm == 0);
    REQUIRE(hand.recordingValue() == 0.0f);

    hand.observeDistance(74);
    REQUIRE(hand.distanceAboveMinimumMm == 0);
    hand.observeDistance(774);
    REQUIRE(hand.distanceAboveMinimumMm == 700);
    REQUIRE(hand.recordingValue() == Catch::Approx(0.5f));
    hand.observeDistance(1400);
    REQUIRE(hand.distanceAboveMinimumMm == 1326);
    // The top of the sensor range intentionally does not record 1.0.
    REQUIRE(hand.recordingValue() == Catch::Approx(1326.0f / 1400.0f));

    for (const int invalidReading : {-1, 0, 73, 1401, 8191})
    {
        hand.observeDistance(774);
        hand.observeDistance(invalidReading);
        REQUIRE(hand.distanceAboveMinimumMm == 0);
        REQUIRE(hand.recordingValue() == 0.0f);
    }
}
