#include <catch2/catch_test_macros.hpp>

#include "scales/scales.h"

TEST_CASE("Scale table has correct number of scales", "[scales]") {
    REQUIRE(SCALES_COUNT == 13);
}

TEST_CASE("Each scale has 48 step entries", "[scales]") {
    REQUIRE(SCALE_STEPS == 48);
}

TEST_CASE("Scale step[0] is always 0 semitones (root)", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        REQUIRE(scale[s][0] == 0);
    }
}

TEST_CASE("Scale values are monotonically non-decreasing", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        for (size_t i = 1; i < SCALE_STEPS; ++i) {
            INFO("Scale " << s << " step " << i);
            REQUIRE(scale[s][i] >= scale[s][i - 1]);
        }
    }
}

TEST_CASE("Scale values are within MIDI semitone range [0, 127]", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        for (size_t i = 0; i < SCALE_STEPS; ++i) {
            REQUIRE(scale[s][i] >= 0);
            REQUIRE(scale[s][i] <= 127);
        }
    }
}

TEST_CASE("Chromatic scale (last scale) has consecutive semitones", "[scales]") {
    const size_t chromatic_idx = SCALES_COUNT - 1;
    for (size_t i = 0; i < 12; ++i) {
        REQUIRE(scale[chromatic_idx][i] == (int)i);
    }
}

TEST_CASE("Ionian (major) scale has correct intervals", "[scales]") {
    // Major scale: 0 2 4 5 7 9 11 12 ...
    const int expected[] = {0, 2, 4, 5, 7, 9, 11, 12};
    for (int i = 0; i < 8; ++i) {
        REQUIRE(scale[0][i] == expected[i]);
    }
}

TEST_CASE("Scale names array is non-null and non-empty", "[scales]") {
    for (size_t s = 0; s < SCALES_COUNT; ++s) {
        REQUIRE(scaleNames[s] != nullptr);
        REQUIRE(scaleNames[s][0] != '\0');
    }
}
